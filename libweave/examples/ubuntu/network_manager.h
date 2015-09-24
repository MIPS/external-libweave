// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBWEAVE_EXAMPLES_UBUNTU_NETWORK_MANAGER_H_
#define LIBWEAVE_EXAMPLES_UBUNTU_NETWORK_MANAGER_H_

#include <string>
#include <vector>

#include <base/memory/weak_ptr.h>
#include <base/time/time.h>
#include <weave/network_provider.h>
#include <weave/wifi_provider.h>

namespace weave {

class TaskRunner;

namespace examples {

// Basic weave::Network implementation.
// Production version of SSL socket needs secure server certificate check.
class NetworkImpl : public NetworkProvider, public WifiProvider {
 public:
  explicit NetworkImpl(TaskRunner* task_runner, bool force_bootstrapping);
  ~NetworkImpl();

  // NetworkProvider implementation.
  void AddConnectionChangedCallback(
      const ConnectionChangedCallback& callback) override;
  NetworkState GetConnectionState() const override;
  void OpenSslSocket(const std::string& host,
                     uint16_t port,
                     const OpenSslSocketSuccessCallback& success_callback,
                     const ErrorCallback& error_callback) override;

  // WifiProvider implementation.
  void Connect(const std::string& ssid,
               const std::string& passphrase,
               const SuccessCallback& success_callback,
               const ErrorCallback& error_callback) override;
  void StartAccessPoint(const std::string& ssid) override;
  void StopAccessPoint() override;

  static bool HasWifiCapability();

 private:
  void TryToConnect(const std::string& ssid,
                    const std::string& passphrase,
                    int pid,
                    base::Time until,
                    const base::Closure& success_callback,
                    const base::Callback<void(const Error*)>& error_callback);
  void UpdateNetworkState();

  bool force_bootstrapping_{false};
  bool hostapd_started_{false};
  TaskRunner* task_runner_{nullptr};
  std::vector<ConnectionChangedCallback> callbacks_;
  NetworkState network_state_{NetworkState::kOffline};

  base::WeakPtrFactory<NetworkImpl> weak_ptr_factory_{this};
};

}  // namespace examples
}  // namespace weave

#endif  // LIBWEAVE_EXAMPLES_UBUNTU_NETWORK_MANAGER_H_
