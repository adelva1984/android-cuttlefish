/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "cuttlefish/host/commands/process_sandboxer/sandbox_manager.h"

#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "absl/memory/memory.h"
#include "absl/random/bit_gen_ref.h"
#include "absl/random/uniform_int_distribution.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "sandboxed_api/sandbox2/executor.h"
#include "sandboxed_api/sandbox2/policy.h"
#include "sandboxed_api/sandbox2/sandbox2.h"
#include "sandboxed_api/util/fileops.h"
#include "sandboxed_api/util/path.h"
#pragma clang diagnostic pop

#include "cuttlefish/host/commands/process_sandboxer/credentialed_unix_server.h"
#include "cuttlefish/host/commands/process_sandboxer/pidfd.h"
#include "cuttlefish/host/commands/process_sandboxer/policies.h"
#include "cuttlefish/host/commands/process_sandboxer/poll_callback.h"
#include "cuttlefish/host/commands/process_sandboxer/proxy_common.h"
#include "cuttlefish/host/commands/process_sandboxer/signal_fd.h"

namespace cuttlefish::process_sandboxer {

using sandbox2::Executor;
using sandbox2::Policy;
using sandbox2::Sandbox2;
using sapi::file::CleanPath;
using sapi::file::JoinPath;
using sapi::file_util::fileops::FDCloser;

namespace {

std::string ServerSocketOutsidePath(std::string_view runtime_dir) {
  return JoinPath(runtime_dir, "/", "server.sock");
}

}  // namespace

class SandboxManager::ProcessNoSandbox : public SandboxManager::ManagedProcess {
 public:
  ProcessNoSandbox(int client_fd, PidFd pid_fd)
      : client_fd_(client_fd), pid_fd_(std::move(pid_fd)) {}
  ~ProcessNoSandbox() {
    auto halt = pid_fd_.HaltHierarchy();
    if (!halt.ok()) {
      LOG(ERROR) << "Failed to halt children: " << halt.ToString();
    }
  }

  std::optional<int> ClientFd() const override { return client_fd_; }
  int PollFd() const override { return pid_fd_.Get(); }

  absl::StatusOr<uintptr_t> ExitCode() override {
    siginfo_t infop;
    idtype_t id_type = (idtype_t)3;  // P_PIDFD
    if (waitid(id_type, pid_fd_.Get(), &infop, WEXITED | WNOWAIT) < 0) {
      return absl::ErrnoToStatus(errno, "`waitid` failed");
    }
    switch (infop.si_code) {
      case CLD_EXITED:
        return infop.si_status;
      case CLD_DUMPED:
      case CLD_KILLED:
        LOG(ERROR) << "Child killed by signal " << infop.si_code;
        return 255;
      default:
        LOG(ERROR) << "Unexpected si_code: " << infop.si_code;
        return 255;
    }
  }

 private:
  int client_fd_;
  PidFd pid_fd_;
};

class SandboxManager::SandboxedProcess : public SandboxManager::ManagedProcess {
 public:
  SandboxedProcess(std::optional<int> client_fd, FDCloser event_fd,
                   std::unique_ptr<Sandbox2> sandbox)
      : client_fd_(client_fd),
        event_fd_(std::move(event_fd)),
        sandbox_(std::move(sandbox)) {
    waiter_thread_ = std::thread([this]() { WaitForExit(); });
  }
  ~SandboxedProcess() override {
    sandbox_->Kill();
    waiter_thread_.join();
    auto res = sandbox_->AwaitResult().ToStatus();
    if (!res.ok()) {
      LOG(ERROR) << "Issue in closing sandbox: '" << res.ToString() << "'";
    }
  }

  std::optional<int> ClientFd() const override { return client_fd_; }
  int PollFd() const override { return event_fd_.get(); }

  absl::StatusOr<uintptr_t> ExitCode() override {
    return sandbox_->AwaitResult().reason_code();
  }

 private:
  void WaitForExit() {
    sandbox_->AwaitResult().IgnoreResult();
    uint64_t buf = 1;
    if (write(event_fd_.get(), &buf, sizeof(buf)) < 0) {
      PLOG(ERROR) << "Failed to write to eventfd";
    }
  }

  std::optional<int> client_fd_;
  FDCloser event_fd_;
  std::thread waiter_thread_;
  std::unique_ptr<Sandbox2> sandbox_;
};

std::string RandomString(absl::BitGenRef gen, std::size_t size) {
  std::stringstream output;
  absl::uniform_int_distribution<char> distribution;
  for (std::size_t i = 0; i < size; i++) {
    output << distribution(gen);
  }
  return output.str();
}

class SandboxManager::SocketClient {
 public:
  SocketClient(SandboxManager& manager, FDCloser client_fd)
      : manager_(manager), client_fd_(std::move(client_fd)) {}
  SocketClient(SocketClient&) = delete;

  int ClientFd() const { return client_fd_.get(); }

  absl::Status HandleMessage() {
    auto message_status = Message::RecvFrom(client_fd_.get());
    if (!message_status.ok()) {
      return message_status.status();
    }
    auto creds_status = UpdateCredentials(message_status->Credentials());
    if (!creds_status.ok()) {
      return creds_status;
    }

    /* This handshake process is to reliably build a `pidfd` based on the pid
     * supplied in the process `ucreds`, through the following steps:
     * 1. Proxy process opens a socket and sends an opening message.
     * 2. Server receives opening message with a kernel-validated `ucreds`
     *    containing the outside-sandbox pid.
     * 3. Server opens a pidfd matching this pid.
     * 4. Server sends a message to the client with some unique data.
     * 5. Client responds with the unique data.
     * 6. Server validates the unique data and credentials match.
     * 7. Server launches a possible sandboxed subprocess based on the pidfd and
     *    /proc/{pid}/
     *
     * Step 5 builds confidence that the pidfd opened in step 3 still
     * corresponds to the client sending messages on the client socket. The
     * pidfd and /proc/{pid} data provide everything necessary to launch the
     * subprocess.
     */
    auto& message = message_status->Data();
    switch (client_state_) {
      case ClientState::kInitial: {
        if (message != kHandshakeBegin) {
          std::string err =
              absl::StrFormat("'%v' != '%v'", kHandshakeBegin, message);
          return absl::InternalError(err);
        }
        pingback_ = RandomString(manager_.bit_gen_, 32);
        absl::StatusOr<std::size_t> stat =
            SendStringMsg(client_fd_.get(), pingback_);
        if (stat.ok()) {
          client_state_ = ClientState::kIgnoredFd;
        }
        return stat.status();
      }
      case ClientState::kIgnoredFd:
        if (!absl::SimpleAtoi(message, &ignored_fd_)) {
          std::string error =
              absl::StrFormat("Expected integer, got '%v'", message);
          return absl::InternalError(error);
        }
        client_state_ = ClientState::kPingback;
        return absl::OkStatus();
      case ClientState::kPingback: {
        if (message != pingback_) {
          std::string err =
              absl::StrFormat("Incorrect '%v' != '%v'", message, pingback_);
          return absl::InternalError(err);
        }
        client_state_ = ClientState::kWaitingForExit;
        return LaunchProcess();
      }
      case ClientState::kWaitingForExit:
        return absl::InternalError("No messages allowed");
    }
  }

  absl::Status SendExitCode(int code) {
    auto send_exit_status = SendStringMsg(client_fd_.get(), "exit");
    if (!send_exit_status.ok()) {
      return send_exit_status.status();
    }

    return SendStringMsg(client_fd_.get(), std::to_string(code)).status();
  }

 private:
  enum class ClientState { kInitial, kIgnoredFd, kPingback, kWaitingForExit };

  absl::Status UpdateCredentials(const std::optional<ucred>& credentials) {
    if (!credentials) {
      return absl::InvalidArgumentError("no creds");
    } else if (!credentials_) {
      credentials_ = credentials;
    } else if (credentials_->pid != credentials->pid) {
      std::string err = absl::StrFormat("pid went from '%d' to '%d'",
                                        credentials_->pid, credentials->pid);
      return absl::PermissionDeniedError(err);
    } else if (credentials_->uid != credentials->uid) {
      return absl::PermissionDeniedError("uid changed");
    } else if (credentials_->gid != credentials->gid) {
      return absl::PermissionDeniedError("gid changed");
    }
    if (!pid_fd_) {
      absl::StatusOr<PidFd> pid_fd =
          PidFd::FromRunningProcess(credentials_->pid);
      if (!pid_fd.ok()) {
        return pid_fd.status();
      }
      pid_fd_ = std::move(*pid_fd);
    }
    return absl::OkStatus();
  }

  absl::Status LaunchProcess() {
    if (!pid_fd_) {
      return absl::InternalError("missing pid_fd_");
    }
    absl::StatusOr<std::vector<std::string>> argv = pid_fd_->Argv();
    if (!argv.ok()) {
      return argv.status();
    }
    if ((*argv)[0] == "openssl") {
      (*argv)[0] = "/usr/bin/openssl";
    }
    absl::StatusOr<std::vector<std::pair<FDCloser, int>>> fds =
        pid_fd_->AllFds();
    if (!fds.ok()) {
      return fds.status();
    }
    absl::StatusOr<std::vector<std::string>> env = pid_fd_->Env();
    if (!env.ok()) {
      return env.status();
    }
    fds->erase(
        std::remove_if(fds->begin(), fds->end(),
                       [this](auto& arg) { return arg.second == ignored_fd_; }),
        fds->end());
    return manager_.RunProcess(client_fd_.get(), std::move(*argv),
                               std::move(*fds), *env);
  }

  SandboxManager& manager_;
  FDCloser client_fd_;
  std::optional<ucred> credentials_;
  std::optional<PidFd> pid_fd_;

  ClientState client_state_ = ClientState::kInitial;
  std::string pingback_;
  int ignored_fd_ = -1;
};

SandboxManager::SandboxManager(HostInfo host_info, std::string runtime_dir,
                               SignalFd signals, CredentialedUnixServer server)
    : host_info_(std::move(host_info)),
      runtime_dir_(std::move(runtime_dir)),
      signals_(std::move(signals)),
      server_(std::move(server)) {}

absl::StatusOr<std::unique_ptr<SandboxManager>> SandboxManager::Create(
    HostInfo host_info) {
  std::string runtime_dir =
      absl::StrFormat("/tmp/sandbox_manager.%u.XXXXXX", getpid());
  if (mkdtemp(runtime_dir.data()) == nullptr) {
    return absl::ErrnoToStatus(errno, "mkdtemp failed");
  }
  VLOG(1) << "Created temporary directory '" << runtime_dir << "'";

  absl::StatusOr<SignalFd> signals = SignalFd::AllExceptSigChld();
  if (!signals.ok()) {
    return signals.status();
  }

  absl::StatusOr<CredentialedUnixServer> server =
      CredentialedUnixServer::Open(ServerSocketOutsidePath(runtime_dir));
  if (!server.ok()) {
    return server.status();
  }

  return absl::WrapUnique(
      new SandboxManager(std::move(host_info), std::move(runtime_dir),
                         std::move(*signals), std::move(*server)));
}

SandboxManager::~SandboxManager() {
  VLOG(1) << "Sandbox shutting down";
  if (!runtime_dir_.empty()) {
    if (unlink(ServerSocketOutsidePath(runtime_dir_).c_str()) < 0) {
      PLOG(ERROR) << "`unlink` failed";
    }
    if (rmdir(runtime_dir_.c_str()) < 0) {
      PLOG(ERROR) << "Failed to remove '" << runtime_dir_ << "'";
    }
  }
}

absl::Status SandboxManager::RunProcess(
    std::optional<int> client_fd, absl::Span<const std::string> argv,
    std::vector<std::pair<FDCloser, int>> fds,
    absl::Span<const std::string> env) {
  if (argv.empty()) {
    return absl::InvalidArgumentError("Not enough arguments");
  }
  bool stdio_mapped[3] = {false, false, false};
  for (const auto& [input_fd, target_fd] : fds) {
    if (0 <= target_fd && target_fd <= 2) {
      stdio_mapped[target_fd] = true;
    }
  }
  // If stdio is not filled in, file descriptors opened by the target process
  // may occupy the standard stdio positions. This can cause unexpected
  for (int i = 0; i <= 2; i++) {
    if (stdio_mapped[i]) {
      continue;
    }
    auto& [stdio_dup, stdio] = fds.emplace_back(dup(i), i);
    if (stdio_dup.get() < 0) {
      return absl::ErrnoToStatus(errno, "Failed to `dup` stdio descriptor");
    }
  }
  std::string exe = CleanPath(argv[0]);
  std::unique_ptr<Policy> policy = PolicyForExecutable(
      host_info_, ServerSocketOutsidePath(runtime_dir_), exe);
  if (policy) {
    return RunSandboxedProcess(client_fd, argv, std::move(fds), env,
                               std::move(policy));
  } else {
    return RunProcessNoSandbox(client_fd, argv, std::move(fds), env);
  }
}

absl::Status SandboxManager::RunSandboxedProcess(
    std::optional<int> client_fd, absl::Span<const std::string> argv,
    std::vector<std::pair<FDCloser, int>> fds,
    absl::Span<const std::string> env, std::unique_ptr<Policy> policy) {
  if (VLOG_IS_ON(1)) {
    std::stringstream process_stream;
    process_stream << "Launching executable with argv: [\n";
    for (const auto& arg : argv) {
      process_stream << "\t\"" << arg << "\",\n";
    }
    process_stream << "] with FD mapping: [\n";
    for (const auto& [fd_in, fd_out] : fds) {
      process_stream << '\t' << fd_in.get() << " -> " << fd_out << ",\n";
    }
    process_stream << "]\n";
    VLOG(1) << process_stream.str();
  }

  std::string exe = CleanPath(argv[0]);
  auto executor = std::make_unique<Executor>(exe, argv, env);
  executor->set_cwd(host_info_.runtime_dir);

  // https://cs.android.com/android/platform/superproject/main/+/main:external/sandboxed-api/sandboxed_api/sandbox2/limits.h;l=116;drc=d451478e26c0352ecd6912461e867a1ae64b17f5
  // Default is 120 seconds
  executor->limits()->set_walltime_limit(absl::InfiniteDuration());
  // Default is 1024 seconds
  executor->limits()->set_rlimit_cpu(RLIM64_INFINITY);

  for (auto& [fd_outer, fd_inner] : fds) {
    // Will close `fd_outer` in this process
    executor->ipc()->MapFd(fd_outer.Release(), fd_inner);
  }

  FDCloser event_fd(eventfd(0, EFD_CLOEXEC));
  if (event_fd.get() < 0) {
    return absl::ErrnoToStatus(errno, "`eventfd` failed");
  }

  auto sbx = std::make_unique<Sandbox2>(std::move(executor), std::move(policy));
  if (!sbx->RunAsync()) {
    return sbx->AwaitResult().ToStatus();
  }

  // A pidfd over the sandbox is another option, but there are two problems:
  //
  // 1. There's a race between launching the sandbox and opening the pidfd. If
  // the sandboxed process exits too quickly, the monitor thread in sandbox2
  // could reap it and another process could reuse the pid before `pidfd_open`
  // runs. Sandbox2 could produce a pidfd itself using `CLONE_PIDFD`, but it
  // does not do this at the time of writing.
  //
  // 2. The sandbox could outlive its top-level process. It's not clear to me if
  // sandbox2 allows this in practice, but `AwaitResult` could theoretically
  // wait on subprocesses of the original sandboxed process as well.
  //
  // To deal with these concerns, we use another thread blocked on AwaitResult
  // that signals the eventfd when sandbox2 says the sandboxed process has
  // exited.

  subprocesses_.emplace_back(
      new SandboxedProcess(client_fd, std::move(event_fd), std::move(sbx)));

  return absl::OkStatus();
}

absl::Status SandboxManager::RunProcessNoSandbox(
    std::optional<int> client_fd, absl::Span<const std::string> argv,
    std::vector<std::pair<FDCloser, int>> fds,
    absl::Span<const std::string> env) {
  if (!client_fd) {
    return absl::InvalidArgumentError("no client for unsandboxed process");
  }

  absl::StatusOr<PidFd> fd = PidFd::LaunchSubprocess(argv, std::move(fds), env);
  if (!fd.ok()) {
    return fd.status();
  }
  subprocesses_.emplace_back(new ProcessNoSandbox(*client_fd, std::move(*fd)));

  return absl::OkStatus();
}

bool SandboxManager::Running() const { return running_; }

absl::Status SandboxManager::Iterate() {
  PollCallback poll_cb;

  poll_cb.Add(signals_.Fd(),
              absl::bind_front(&SandboxManager::Signalled, this));
  poll_cb.Add(server_.Fd(), absl::bind_front(&SandboxManager::NewClient, this));

  for (auto it = subprocesses_.begin(); it != subprocesses_.end(); it++) {
    int fd = (*it)->PollFd();
    poll_cb.Add(fd, absl::bind_front(&SandboxManager::ProcessExit, this, it));
  }
  for (auto it = clients_.begin(); it != clients_.end(); it++) {
    int fd = (*it)->ClientFd();
    poll_cb.Add(fd, absl::bind_front(&SandboxManager::ClientMessage, this, it));
  }

  return poll_cb.Poll();
}

absl::Status SandboxManager::Signalled(short revents) {
  if (revents != POLLIN) {
    running_ = false;
    return absl::InternalError("signalfd exited");
  }

  absl::StatusOr<signalfd_siginfo> info = signals_.ReadSignal();
  if (!info.ok()) {
    return info.status();
  }
  VLOG(1) << "Received signal with signo '" << info->ssi_signo << "'";

  switch (info->ssi_signo) {
    case SIGHUP:
    case SIGINT:
    case SIGTERM:
      LOG(INFO) << "Received signal '" << info->ssi_signo << "', exiting";
      running_ = false;
      return absl::OkStatus();
    default:
      std::string err = absl::StrCat("Unexpected signal ", info->ssi_signo);
      return absl::InternalError(err);
  }
}

absl::Status SandboxManager::NewClient(short revents) {
  if (revents != POLLIN) {
    running_ = false;
    return absl::InternalError("server socket exited");
  }
  absl::StatusOr<FDCloser> client = server_.AcceptClient();
  if (!client.ok()) {
    return client.status();
  }
  clients_.emplace_back(new SocketClient(*this, std::move(*client)));
  return absl::OkStatus();
}

absl::Status SandboxManager::ProcessExit(SandboxManager::SboxIter it,
                                         short revents) {
  if ((*it)->ClientFd()) {
    int client_fd = *(*it)->ClientFd();
    for (auto& client : clients_) {
      if (client->ClientFd() != client_fd) {
        continue;
      }
      auto exit_code = (*it)->ExitCode();
      if (!exit_code.ok()) {
        LOG(ERROR) << exit_code.status();
      }
      // TODO(schuffelen): Forward more complete exit information
      auto send_res = client->SendExitCode(exit_code.value_or(254));
      if (!send_res.ok()) {
        return send_res;
      }
    }
  }
  subprocesses_.erase(it);
  if (subprocesses_.empty()) {
    running_ = false;
  }
  static constexpr char kErr[] = "eventfd exited";
  return revents == POLLIN ? absl::OkStatus() : absl::InternalError(kErr);
}

absl::Status SandboxManager::ClientMessage(SandboxManager::ClientIter it,
                                           short rev) {
  if (rev == POLLIN) {
    return (*it)->HandleMessage();
  }
  clients_.erase(it);
  return absl::InternalError("client dropped file descriptor");
}

}  // namespace cuttlefish::process_sandboxer
