#pragma once
#include <stddef.h>
#include <stdint.h>

enum class LinkState : uint8_t { Searching, Connecting, Connected, Lost };
enum class WifiScanState : uint8_t { Idle, Scanning, Complete, Failed };

struct WifiNetwork {
  char ssid[33];
  int rssi;
  bool secure;
};

class Link {
public:
  virtual ~Link() {}
  virtual void begin() = 0;
  virtual void loop() = 0;                                  // called repeatedly from the link task
  virtual LinkState state() const = 0;
  virtual bool send(const uint8_t* d, size_t n) = 0;        // queues bytes; false (and drops them) if not connected
  virtual void scanWifiNetworks() {}
  virtual WifiScanState wifiScanState() const { return WifiScanState::Failed; }
  virtual int wifiNetworkCount() const { return 0; }
  virtual bool wifiNetwork(int index, WifiNetwork& network) const { (void)index; (void)network; return false; }
  virtual bool setWifiCredentials(const char* ssid, const char* password) {
    (void)ssid; (void)password; return false;
  }
};
