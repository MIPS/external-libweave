// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBWEAVE_SRC_PRIVET_WIFI_BOOTSTRAP_MANAGER_H_
#define LIBWEAVE_SRC_PRIVET_WIFI_BOOTSTRAP_MANAGER_H_

#include <set>
#include <string>
#include <vector>

#include <base/callback.h>
#include <base/macros.h>
#include <base/memory/weak_ptr.h>
#include <base/scoped_observer.h>

#include "libweave/src/privet/cloud_delegate.h"
#include "libweave/src/privet/privet_types.h"
#include "libweave/src/privet/wifi_delegate.h"
#include "libweave/src/privet/wifi_ssid_generator.h"

namespace weave {

class Network;

namespace privet {

class CloudDelegate;
class DeviceDelegate;

class WifiBootstrapManager : public WifiDelegate,
                             public CloudDelegate::Observer {
 public:
  using State = WifiSetupState;

  using StateListener = base::Callback<void(State)>;

  WifiBootstrapManager(const std::string& last_configured_ssid,
                       const std::string& test_privet_ssid,
                       bool wifi_setup_enabled,
                       TaskRunner* task_runner,
                       Network* shill_client,
                       CloudDelegate* gcd);
  ~WifiBootstrapManager() override = default;
  virtual void Init();
  void RegisterStateListener(const StateListener& listener);

  // Overrides from WifiDelegate.
  const ConnectionState& GetConnectionState() const override;
  const SetupState& GetSetupState() const override;
  bool ConfigureCredentials(const std::string& ssid,
                            const std::string& passphrase,
                            ErrorPtr* error) override;
  std::string GetCurrentlyConnectedSsid() const override;
  std::string GetHostedSsid() const override;
  std::set<WifiType> GetTypes() const override;

  // Overrides from CloudDelegate::Observer.
  void OnDeviceInfoChanged() override;

 private:
  // These Start* tasks:
  //   1) Do state appropriate work for entering the indicated state.
  //   2) Update the state variable to reflect that we're in a new state
  //   3) Call StateListeners to notify that we've transitioned.
  // These End* tasks perform cleanup on leaving indicated state.
  void StartBootstrapping();
  void EndBootstrapping();

  void StartConnecting(const std::string& ssid, const std::string& passphrase);
  void EndConnecting();

  void StartMonitoring();
  void EndMonitoring();

  // Update the current state, post tasks to notify listeners accordingly to
  // the MessageLoop.
  void UpdateState(State new_state);
  void NotifyStateListeners(State new_state) const;

  std::string GenerateSsid() const;

  // If we've been bootstrapped successfully before, and we're bootstrapping
  // again because we slipped offline for a sufficiently longtime, we want
  // to return to monitoring mode periodically in case our connectivity issues
  // were temporary.
  void OnBootstrapTimeout();
  void OnConnectTimeout();
  void OnConnectSuccess(const std::string& ssid);
  void OnConnectivityChange(bool is_connected);
  void OnMonitorTimeout();
  void UpdateConnectionState();

  // Initialization could be delayed if ssid_generator_ is not ready.
  bool is_initialized_{false};
  State state_{State::kDisabled};
  // Setup state is the temporal state of the most recent bootstrapping attempt.
  // It is not persisted to disk.
  SetupState setup_state_{SetupState::kNone};
  ConnectionState connection_state_{ConnectionState::kDisabled};
  TaskRunner* task_runner_{nullptr};
  Network* network_{nullptr};
  WifiSsidGenerator ssid_generator_;
  base::Time monitor_until_;

  std::vector<StateListener> state_listeners_;
  bool currently_online_{false};
  std::string last_configured_ssid_;
  std::string test_privet_ssid_;
  std::string privet_ssid_;
  bool ble_setup_enabled_{false};

  ScopedObserver<CloudDelegate, CloudDelegate::Observer> cloud_observer_{this};

  // Helps to reset irrelevant tasks switching state.
  base::WeakPtrFactory<WifiBootstrapManager> tasks_weak_factory_{this};

  base::WeakPtrFactory<WifiBootstrapManager> lifetime_weak_factory_{this};

  DISALLOW_COPY_AND_ASSIGN(WifiBootstrapManager);
};

}  // namespace privet
}  // namespace weave

#endif  // LIBWEAVE_SRC_PRIVET_WIFI_BOOTSTRAP_MANAGER_H_
