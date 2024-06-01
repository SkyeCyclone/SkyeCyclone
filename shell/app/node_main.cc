// Copyright (c) 2015 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/app/node_main.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/threading/thread_task_runner_handle.h"
#include "content/public/common/content_switches.h"
#include "electron/electron_version.h"
#include "gin/array_buffer.h"
#include "gin/public/isolate_holder.h"
#include "gin/v8_initializer.h"
#include "shell/app/uv_task_runner.h"
#include "shell/browser/javascript_environment.h"
#include "shell/browser/node_debugger.h"
#include "shell/common/api/electron_bindings.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/node_bindings.h"
#include "shell/common/node_includes.h"

#if defined(OS_LINUX)
#include "components/crash/core/app/breakpad_linux.h"
#endif

#if defined(OS_WIN)
#include "chrome/child/v8_crashpad_support_win.h"
#endif

#if !defined(MAS_BUILD)
#include "components/crash/core/app/crashpad.h"  // nogncheck
#include "shell/app/electron_crash_reporter_client.h"
#include "shell/browser/api/electron_api_crash_reporter.h"
#include "shell/common/crash_keys.h"
#endif

namespace {

// Initialize Node.js cli options to pass to Node.js
// See https://nodejs.org/api/cli.html#cli_options
void SetNodeCliFlags() {
  // Options that are unilaterally disallowed
  const std::unordered_set<base::StringPiece, base::StringPieceHash>
      disallowed = {"--openssl-config", "--use-bundled-ca", "--use-openssl-ca",
                    "--force-fips", "--enable-fips"};

  const auto argv = base::CommandLine::ForCurrentProcess()->argv();
  std::vector<std::string> args;

  // TODO(codebytere): We need to set the first entry in args to the
  // process name owing to src/node_options-inl.h#L286-L290 but this is
  // redundant and so should be refactored upstream.
  args.reserve(argv.size() + 1);
  args.emplace_back("electron");

  for (const auto& arg : argv) {
#if defined(OS_WIN)
    const auto& option = base::UTF16ToUTF8(arg);
#else
    const auto& option = arg;
#endif
    const auto stripped = base::StringPiece(option).substr(0, option.find('='));
    if (disallowed.count(stripped) != 0) {
      LOG(ERROR) << "The Node.js cli flag " << stripped
                 << " is not supported in Electron";
    } else {
      args.push_back(option);
    }
  }

  std::vector<std::string> errors;

  // Node.js itself will output parsing errors to
  // console so we don't need to handle that ourselves
  ProcessGlobalArgs(&args, nullptr, &errors, node::kDisallowedInEnvironment);
}

#if defined(MAS_BUILD)
void SetCrashKeyStub(const std::string& key, const std::string& value) {}
void ClearCrashKeyStub(const std::string& key) {}
#endif

}  // namespace

namespace electron {

#if defined(OS_LINUX)
void CrashReporterStart(gin_helper::Dictionary options) {
  std::string submit_url;
  bool upload_to_server = true;
  bool ignore_system_crash_handler = false;
  bool rate_limit = false;
  bool compress = false;
  std::map<std::string, std::string> global_extra;
  std::map<std::string, std::string> extra;
  options.Get("submitURL", &submit_url);
  options.Get("uploadToServer", &upload_to_server);
  options.Get("ignoreSystemCrashHandler", &ignore_system_crash_handler);
  options.Get("rateLimit", &rate_limit);
  options.Get("compress", &compress);
  options.Get("extra", &extra);
  options.Get("globalExtra", &global_extra);

  std::string product_name;
  if (options.Get("productName", &product_name))
    global_extra["_productName"] = product_name;
  std::string company_name;
  if (options.Get("companyName", &company_name))
    global_extra["_companyName"] = company_name;
  api::crash_reporter::Start(submit_url, upload_to_server,
                             ignore_system_crash_handler, rate_limit, compress,
                             global_extra, extra, true);
}
#endif

v8::Local<v8::Value> GetParameters(v8::Isolate* isolate) {
  std::map<std::string, std::string> keys;
#if !defined(MAS_BUILD)
  electron::crash_keys::GetCrashKeys(&keys);
#endif
  return gin::ConvertToV8(isolate, keys);
}

int NodeMain(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);

#if defined(OS_WIN)
  v8_crashpad_support::SetUp();
#endif

#if !defined(MAS_BUILD)
  ElectronCrashReporterClient::Create();
#endif

#if defined(OS_WIN) || (defined(OS_MAC) && !defined(MAS_BUILD))
  crash_reporter::InitializeCrashpad(false, "node");
#endif

#if !defined(MAS_BUILD)
  crash_keys::SetCrashKeysFromCommandLine(
      *base::CommandLine::ForCurrentProcess());
  crash_keys::SetPlatformCrashKey();
#endif

  int exit_code = 1;
  {
    // Feed gin::PerIsolateData with a task runner.
    argv = uv_setup_args(argc, argv);
    uv_loop_t* loop = uv_default_loop();
    scoped_refptr<UvTaskRunner> uv_task_runner(new UvTaskRunner(loop));
    base::ThreadTaskRunnerHandle handle(uv_task_runner);

    // Initialize feature list.
    auto feature_list = std::make_unique<base::FeatureList>();
    feature_list->InitializeFromCommandLine("", "");
    base::FeatureList::SetInstance(std::move(feature_list));

    // Explicitly register electron's builtin modules.
    NodeBindings::RegisterBuiltinModules();

    // Parse and set Node.js cli flags.
    SetNodeCliFlags();

    int exec_argc;
    const char** exec_argv;
    node::Init(&argc, const_cast<const char**>(argv), &exec_argc, &exec_argv);

    gin::V8Initializer::LoadV8Snapshot(
        gin::V8Initializer::V8SnapshotFileType::kWithAdditionalContext);

    // V8 requires a task scheduler apparently
    base::ThreadPoolInstance::CreateAndStartWithDefaultParams("Electron");

    // Initialize gin::IsolateHolder.
    JavascriptEnvironment gin_env(loop);

    v8::Isolate* isolate = gin_env.isolate();

    v8::Isolate::Scope isolate_scope(isolate);
    v8::Locker locker(isolate);
    node::Environment* env = nullptr;
    node::IsolateData* isolate_data = nullptr;
    {
      v8::HandleScope scope(isolate);

      isolate_data = node::CreateIsolateData(isolate, loop, gin_env.platform());
      CHECK_NE(nullptr, isolate_data);

      env = node::CreateEnvironment(isolate_data, gin_env.context(), argc, argv,
                                    exec_argc, exec_argv);
      CHECK_NE(nullptr, env);

      // This needs to be called before the inspector is initialized.
      env->InitializeDiagnostics();

      node::IsolateSettings is;
      node::SetIsolateUpForNode(isolate, is);

      gin_helper::Dictionary process(isolate, env->process_object());
#if defined(OS_WIN)
      process.SetMethod("log", &ElectronBindings::Log);
#endif
      process.SetMethod("crash", &ElectronBindings::Crash);

      // Setup process.crashReporter in child node processes
      gin_helper::Dictionary reporter = gin::Dictionary::CreateEmpty(isolate);
#if defined(OS_LINUX)
      reporter.SetMethod("start", &CrashReporterStart);
#endif

      reporter.SetMethod("getParameters", &GetParameters);
#if defined(MAS_BUILD)
      reporter.SetMethod("addExtraParameter", &SetCrashKeyStub);
      reporter.SetMethod("removeExtraParameter", &ClearCrashKeyStub);
#else
      reporter.SetMethod("addExtraParameter",
                         &electron::crash_keys::SetCrashKey);
      reporter.SetMethod("removeExtraParameter",
                         &electron::crash_keys::ClearCrashKey);
#endif

      process.Set("crashReporter", reporter);

      gin_helper::Dictionary versions;
      if (process.Get("versions", &versions)) {
        versions.SetReadOnly(ELECTRON_PROJECT_NAME, ELECTRON_VERSION_STRING);
      }
    }

    // Enable support for v8 inspector.
    NodeDebugger node_debugger(env);
    node_debugger.Start();

    v8::HandleScope scope(isolate);
    node::LoadEnvironment(env);

    env->set_trace_sync_io(env->options()->trace_sync_io);

    {
      v8::SealHandleScope seal(isolate);
      bool more;
      do {
        uv_run(env->event_loop(), UV_RUN_DEFAULT);

        gin_env.platform()->DrainTasks(isolate);

        more = uv_loop_alive(env->event_loop());
        if (more && !env->is_stopping())
          continue;

        if (!uv_loop_alive(env->event_loop())) {
          EmitBeforeExit(env);
        }

        // Emit `beforeExit` if the loop became alive either after emitting
        // event, or after running some callbacks.
        more = uv_loop_alive(env->event_loop());
      } while (more && !env->is_stopping());
    }

    node_debugger.Stop();

    env->set_trace_sync_io(false);

    exit_code = node::EmitExit(env);

    node::ResetStdio();

    node::Stop(env);
    env->stop_sub_worker_contexts();
    env->RunCleanup();

    node::RunAtExit(env);
    node::FreeEnvironment(env);
    node::FreeIsolateData(isolate_data);

    gin_env.platform()->DrainTasks(isolate);
    gin_env.platform()->CancelPendingDelayedTasks(isolate);
    gin_env.platform()->UnregisterIsolate(isolate);
  }

  // According to "src/gin/shell/gin_main.cc":
  //
  // gin::IsolateHolder waits for tasks running in ThreadPool in its
  // destructor and thus must be destroyed before ThreadPool starts skipping
  // CONTINUE_ON_SHUTDOWN tasks.
  base::ThreadPoolInstance::Get()->Shutdown();

  v8::V8::Dispose();

  return exit_code;
}

}  // namespace electron
