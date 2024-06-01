// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_AUTO_UPDATER_H_
#define SHELL_BROWSER_AUTO_UPDATER_H_

#include <map>
#include <string>

#include "base/macros.h"
#include "build/build_config.h"

namespace base {
class Time;
}

namespace gin {
class Arguments;
}

namespace auto_updater {

class Delegate {
 public:
  // An error happened.
  virtual void OnError(const std::string& error) {}

  virtual void OnError(const std::string& error,
                       const int code,
                       const std::string& domain) {}

  // Checking to see if there is an update
  virtual void OnCheckingForUpdate() {}

  // There is an update available and it is being downloaded
  virtual void OnUpdateAvailable() {}

  // There is no available update.
  virtual void OnUpdateNotAvailable() {}

  // There is a new update which has been downloaded.
  virtual void OnUpdateDownloaded(const std::string& release_notes,
                                  const std::string& release_name,
                                  const base::Time& release_date,
                                  const std::string& update_url) {}

 protected:
  virtual ~Delegate() {}
};

class AutoUpdater {
 public:
  typedef std::map<std::string, std::string> HeaderMap;

  // Gets/Sets the delegate.
  static Delegate* GetDelegate();
  static void SetDelegate(Delegate* delegate);

  static std::string GetFeedURL();
  // FIXME(zcbenz): We should not do V8 in this file, this method should only
  // accept C++ struct as parameter, and atom_api_auto_updater.cc is responsible
  // for parsing the parameter from JavaScript.
  static void SetFeedURL(gin::Arguments* args);
  static void CheckForUpdates();
  static void QuitAndInstall();

 private:
  static Delegate* delegate_;

  DISALLOW_IMPLICIT_CONSTRUCTORS(AutoUpdater);
};

}  // namespace auto_updater

#endif  // SHELL_BROWSER_AUTO_UPDATER_H_
