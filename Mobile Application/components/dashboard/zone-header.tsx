"use client"

import { useState, useEffect } from "react"
import { cn } from "@/lib/utils"
import { Badge } from "@/components/ui/badge"
import { Wifi, WifiOff, AlertCircle, Plus, QrCode, Keyboard, Camera, Loader2, Bluetooth } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Capacitor } from "@capacitor/core"
import { BarcodeScanner, BarcodeFormat } from "@capacitor-mlkit/barcode-scanning"
import { BleClient } from "@capacitor-community/bluetooth-le"

const BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
const BLE_CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs"
import type { Zone } from "./zone-sidebar"

interface ZoneHeaderProps {
  zone: Zone
  onAddDevice?: (name: string, mac: string, pin: string) => Promise<{ success: boolean; error?: string }>
}

export function ZoneHeader({ zone, onAddDevice }: ZoneHeaderProps) {
  const devices = zone.devices || []
  const onlineCount = devices.filter((d) => d.isOnline).length
  const totalCount = devices.length
  const allOnline = onlineCount === totalCount && totalCount > 0
  const allOffline = onlineCount === 0

  const [isOpen, setIsOpen] = useState(false)
  const defaultDeviceName = `${zone.name} - ${totalCount + 1}`
  const [deviceName, setDeviceName] = useState(defaultDeviceName)
  const [macAddress, setMacAddress] = useState("")
  const [pin, setPin] = useState("")
  const [loading, setLoading] = useState(false)
  const [errorMsg, setErrorMsg] = useState("")
  const [activeTab, setActiveTab] = useState("qr")

  // BLE provisioning states
  const [bleDevices, setBleDevices] = useState<any[]>([])
  const [scanningBle, setScanningBle] = useState(false)
  const [connectedBleId, setConnectedBleId] = useState<string | null>(null)
  const [wifiSsid, setWifiSsid] = useState("")
  const [wifiPassword, setWifiPassword] = useState("")
  const [bleStatus, setBleStatus] = useState("")

  // Reset name when zone changes or modal opens
  useEffect(() => {
    if (isOpen) {
      setDeviceName(`${zone.name} - ${devices.length + 1}`)
      setMacAddress("")
      setPin("")
      setErrorMsg("")
      setLoading(false)
      setBleDevices([])
      setScanningBle(false)
      setConnectedBleId(null)
      setWifiSsid("")
      setWifiPassword("")
      setBleStatus("")
      setActiveTab("qr")
    }
  }, [isOpen, zone, devices.length])

  // Cleanup BLE connection on unmount or disconnect
  useEffect(() => {
    return () => {
      if (connectedBleId) {
        BleClient.disconnect(connectedBleId).catch(() => {});
      }
    };
  }, [connectedBleId]);

  const handleAdd = async () => {
    const trimmedMac = macAddress.trim().toUpperCase().replace(/[-:]/g, "");
    const trimmedPin = pin.trim();

    if (!trimmedMac) {
      setErrorMsg("Please enter a hardware code / MAC Address.");
      return;
    }

    if (!trimmedPin) {
      setErrorMsg("Please enter the pairing PIN.");
      return;
    }

    if (onAddDevice) {
      setLoading(true);
      setErrorMsg("");
      try {
        const result = await onAddDevice(deviceName || defaultDeviceName, trimmedMac, trimmedPin);
        if (result.success) {
          setIsOpen(false);
        } else {
          setErrorMsg(result.error || "Pairing failed. Please verify credentials.");
        }
      } catch (err: any) {
        setErrorMsg("Failed to communicate with pairing service.");
      } finally {
        setLoading(false);
      }
    }
  }

  const handleScannedUri = (value: string) => {
    let mac = "";
    let pin = "";
    
    try {
      if (value.startsWith("{")) {
        const data = JSON.parse(value);
        mac = data.mac || "";
        pin = data.pin || "";
      } else if (value.includes("mac=") && value.includes("pin=")) {
        const url = new URL(value.replace("zone://", "http://"));
        mac = url.searchParams.get("mac") || "";
        pin = url.searchParams.get("pin") || "";
      } else if (value.includes(",")) {
        const parts = value.split(",");
        mac = parts[0] || "";
        pin = parts[1] || "";
      } else {
        mac = value;
      }
    } catch (e) {
      console.error("Parsing failed:", e);
      setErrorMsg("Failed to parse QR code format.");
      return;
    }

    const cleanedMac = mac.trim().toUpperCase().replace(/[-:]/g, "");
    const cleanedPin = pin.trim();

    if (!cleanedMac) {
      setErrorMsg("Scanned QR code does not contain a valid MAC Address.");
      return;
    }

    setMacAddress(cleanedMac);
    setPin(cleanedPin);
    
    if (cleanedMac && cleanedPin) {
      setLoading(true);
      setErrorMsg("");
      if (onAddDevice) {
        onAddDevice(deviceName || defaultDeviceName, cleanedMac, cleanedPin)
          .then((result) => {
            if (result.success) {
              setIsOpen(false);
            } else {
              setErrorMsg(result.error || "Scanned QR pairing failed.");
            }
          })
          .catch(() => {
            setErrorMsg("Failed to communicate with pairing service.");
          })
          .finally(() => {
            setLoading(false);
          });
      }
    }
  }

  const waitForInstallation = () => {
    return new Promise<void>(async (resolve, reject) => {
      let installListener: any = null;
      
      const timeoutId = setTimeout(() => {
        if (installListener) {
          installListener.remove();
        }
        reject(new Error("Scanner module installation timed out. Please check your internet/Google Play connection."));
      }, 60000);

      try {
        installListener = await BarcodeScanner.addListener(
          'googleBarcodeScannerModuleInstallProgress',
          (event: any) => {
            const { state, progress } = event;
            
            if (state === 4) { // COMPLETED
              clearTimeout(timeoutId);
              installListener.remove();
              resolve();
            } else if (state === 5) { // FAILED
              clearTimeout(timeoutId);
              installListener.remove();
              reject(new Error("Scanner module installation failed. Please check Google Play Services."));
            } else if (state === 3) { // CANCELED
              clearTimeout(timeoutId);
              installListener.remove();
              reject(new Error("Scanner module installation was canceled."));
            } else {
              const stateNames = ["Unknown", "Pending", "Downloading", "Canceled", "Completed", "Failed", "Installing"];
              const stateName = stateNames[state] || "Unknown";
              setErrorMsg(`Installing scanner: ${stateName} (${progress}%)`);
            }
          }
        );

        await BarcodeScanner.installGoogleBarcodeScannerModule();
      } catch (err) {
        clearTimeout(timeoutId);
        if (installListener) {
          installListener.remove();
        }
        reject(err);
      }
    });
  };

  const handleScanQR = async () => {
    try {
      const permission = await BarcodeScanner.requestPermissions();
      if (permission.camera !== 'granted' && permission.camera !== 'limited') {
        setErrorMsg("Camera permission is required to scan QR codes.");
        return;
      }

      // Check if Android and install google barcode scanner module if not available
      if (Capacitor.isNativePlatform() && Capacitor.getPlatform() === 'android') {
        const { available } = await BarcodeScanner.isGoogleBarcodeScannerModuleAvailable();
        if (!available) {
          setLoading(true);
          setErrorMsg("Downloading and installing barcode scanner module (via Google Play Services)...");
          try {
            await waitForInstallation();
            setErrorMsg("Scanner module ready! Opening camera...");
          } catch (installErr: any) {
            setErrorMsg(installErr.message || "Failed to install scanner module.");
            setLoading(false);
            return;
          } finally {
            setLoading(false);
          }
        }
      }

      const { barcodes } = await BarcodeScanner.scan({
        formats: [BarcodeFormat.QrCode],
      });

      if (barcodes && barcodes.length > 0) {
        handleScannedUri(barcodes[0].rawValue);
      }
    } catch (err: any) {
      console.error("Scanner error:", err);
      setErrorMsg(`Scanner failed: ${err.message || err}`);
    }
  }

  const startBleScan = async () => {
    setBleDevices([]);
    setScanningBle(true);
    setBleStatus("Initializing Bluetooth...");
    setErrorMsg("");

    try {
      await BleClient.initialize({ androidNeverForLocation: true });
      
      // Prompt user to enable Bluetooth on Android if disabled
      const isEnabledResult = await BleClient.isEnabled();
      if (!isEnabledResult.value) {
        if (Capacitor.getPlatform() === 'android') {
          try {
            await BleClient.requestEnable();
            
            // Poll for Bluetooth to actually turn on (up to 50 retries = 5 seconds)
            let retries = 50;
            while (retries > 0) {
              const check = await BleClient.isEnabled();
              if (check.value) break;
              await new Promise((resolve) => setTimeout(resolve, 100));
              retries--;
            }
          } catch (e) {
            console.error("Failed to prompt Bluetooth activation:", e);
            throw new Error("Bluetooth activation request was denied or failed.");
          }
        } else {
          // On other platforms, verify and throw if disabled
          const isEnabledCheck = await BleClient.isEnabled();
          if (!isEnabledCheck.value) {
            throw new Error("Bluetooth is disabled. Please turn on Bluetooth to scan for devices.");
          }
        }
      }
      
      setBleStatus("Scanning for ZONE devices...");
      
      await BleClient.requestLEScan(
        {},
        (result) => {
          if (result.device) {
            const devName = (result.device.name || result.localName || "").toUpperCase();
            if (devName.includes("ZONE")) {
              setBleDevices((prev) => {
                if (prev.some((d) => d.deviceId === result.device.deviceId)) {
                  return prev;
                }
                return [...prev, result.device];
              });
            }
          }
        }
      );

      // Stop scanning after 10 seconds automatically
      setTimeout(async () => {
        try {
          await BleClient.stopLEScan();
        } catch (e) {}
        setScanningBle(false);
        setBleStatus((prev) => prev === "Scanning for ZONE devices..." ? "Scan finished." : prev);
      }, 10000);

    } catch (err: any) {
      console.error("BLE scan error:", err);
      setErrorMsg(`Bluetooth Error: ${err.message || err}`);
      setScanningBle(false);
      setBleStatus("");
    }
  };

  const connectAndProvisionBle = async (device: any) => {
    setLoading(true);
    setBleStatus(`Connecting to ${device.name || "ZONE-ESP32"}...`);
    setErrorMsg("");
    
    try {
      await BleClient.connect(device.deviceId, (disconnectedId) => {
        console.log(`Device ${disconnectedId} disconnected.`);
        setConnectedBleId(null);
        setBleStatus("Disconnected.");
        setLoading(false);
      });

      try {
        await BleClient.requestMtu(device.deviceId, 512);
      } catch (mtuErr) {
        console.warn("Failed to request MTU:", mtuErr);
      }

      setConnectedBleId(device.deviceId);
      setBleStatus(`Connected to ${device.name || "ZONE-ESP32"}!`);
    } catch (err: any) {
      console.error("BLE connection error:", err);
      setErrorMsg(`Connection failed: ${err.message || err}`);
      setConnectedBleId(null);
      setBleStatus("");
    } finally {
      setLoading(false);
    }
  };

  const handleBleProvision = async () => {
    if (!connectedBleId) {
      setErrorMsg("Please scan and connect to a ZONE-ESP32 device first.");
      return;
    }
    if (!macAddress) {
      setErrorMsg("Please enter the Device MAC Address.");
      return;
    }
    if (!pin) {
      setErrorMsg("Please enter the Pairing PIN.");
      return;
    }
    if (!wifiSsid) {
      setErrorMsg("Please enter your Wi-Fi SSID.");
      return;
    }

    setLoading(true);
    setBleStatus("Sending Wi-Fi credentials over Bluetooth...");
    setErrorMsg("");

    try {
      const encoder = new TextEncoder();
      const bytes = encoder.encode(`${wifiSsid},${wifiPassword}`);
      const dataView = new DataView(bytes.buffer);

      await BleClient.write(
        connectedBleId,
        BLE_SERVICE_UUID,
        BLE_CHARACTERISTIC_UUID,
        dataView
      );

      setBleStatus("Credentials received! ESP32 is rebooting. Pairing on dashboard...");

      const trimmedMac = macAddress.trim().toUpperCase().replace(/[-:]/g, "");
      const trimmedPin = pin.trim();

      if (onAddDevice) {
        const result = await onAddDevice(deviceName || defaultDeviceName, trimmedMac, trimmedPin);
        if (result.success) {
          setBleStatus("Pairing successful! Device configured.");
          try {
            await BleClient.disconnect(connectedBleId);
          } catch (e) {}
          setConnectedBleId(null);
          setTimeout(() => {
            setIsOpen(false);
          }, 1500);
        } else {
          setErrorMsg(result.error || "BLE configuration sent, but dashboard pairing failed. Check credentials.");
          try {
            await BleClient.disconnect(connectedBleId);
          } catch (e) {}
          setConnectedBleId(null);
        }
      }
    } catch (err: any) {
      console.error("BLE provisioning error:", err);
      setErrorMsg(`Failed to provision device: ${err.message || err}`);
      if (connectedBleId) {
        try {
          await BleClient.disconnect(connectedBleId);
        } catch (e) {}
        setConnectedBleId(null);
      }
    } finally {
      setLoading(false);
    }
  };



  return (
    <header className="flex flex-wrap items-center justify-between gap-4 border-b border-white/5 bg-white/2 backdrop-blur-xl px-6 py-8 lg:px-10 z-10 relative">
      <div className="flex items-center gap-4">
        <div>
          <h2 className="text-3xl font-bold tracking-tight text-white lg:text-4xl">
            {zone.name}
          </h2>
          <p className="mt-1 text-sm text-white/50">{zone.location}</p>
        </div>

        <Badge
          variant="outline"
          className="ml-2 gap-1.5 px-2.5 py-1 text-xs font-medium rounded-full border-white/10 bg-white/5 text-white"
        >
          {allOnline ? (
            <>
              <Wifi className="h-3 w-3" />
              All Online
            </>
          ) : allOffline ? (
            <>
              <WifiOff className="h-3 w-3" />
              All Offline
            </>
          ) : (
            <>
              <AlertCircle className="h-3 w-3" />
              {onlineCount}/{totalCount} Online
            </>
          )}
        </Badge>
      </div>

      <div className="flex items-center gap-6">
        <div className="text-right">
          <p className="text-sm font-medium text-white/60">
            {devices.filter((d) => d.isEnabled && d.isOnline).length} device{devices.filter((d) => d.isEnabled && d.isOnline).length !== 1 ? "s" : ""} active
          </p>
        </div>
        
        {/* Add Device Modal */}
        <Dialog open={isOpen} onOpenChange={setIsOpen}>
          <DialogTrigger asChild>
            <Button className="gap-2 clay-gradient-btn shadow-md">
              <Plus className="h-4 w-4" />
              Add Device
            </Button>
          </DialogTrigger>
          <DialogContent className="sm:max-w-[500px] border-white/10 bg-[#161224]/95 text-white backdrop-blur-2xl">
            <DialogHeader>
              <DialogTitle className="text-white border-b border-white/5 pb-4">Pair New Device</DialogTitle>
              <DialogDescription className="pt-2 text-white/50">
                Scan the QR code on the physical hardware or enter the MAC/Code manually to pair it with {zone.name}.
              </DialogDescription>
            </DialogHeader>
            
            <div className="py-2 space-y-6">
              {errorMsg && (
                <div className="flex items-center gap-2 rounded-lg bg-destructive/10 border border-destructive/20 p-3 text-xs text-destructive">
                  <AlertCircle className="h-4 w-4 shrink-0" />
                  <p>{errorMsg}</p>
                </div>
              )}

              <div className="space-y-2">
                <Label htmlFor="deviceName" className="text-white">Device Name</Label>
                <Input 
                  id="deviceName" 
                  value={deviceName}
                  onChange={(e) => setDeviceName(e.target.value)}
                  className="bg-black/40 border-white/10 text-white focus-visible:ring-white/20 text-base"
                />
              </div>

              <Tabs defaultValue="qr" onValueChange={setActiveTab} className="w-full">
                <TabsList className="grid w-full grid-cols-2 bg-black/40 border border-white/10 p-1">
                  <TabsTrigger value="qr" className="data-[state=active]:bg-white/10 data-[state=active]:text-white flex items-center gap-2 text-xs">
                    <QrCode className="h-3.5 w-3.5" /> QR Code
                  </TabsTrigger>
                  <TabsTrigger value="manual" className="data-[state=active]:bg-white/10 data-[state=active]:text-white flex items-center gap-2 text-xs">
                    <Keyboard className="h-3.5 w-3.5" /> Manual Setup
                  </TabsTrigger>
                </TabsList>
                
                <TabsContent value="qr" className="mt-4">
                  {Capacitor.isNativePlatform() ? (
                    <div className="flex flex-col items-center justify-center rounded-xl border border-white/5 bg-black/20 py-8 px-4 text-center">
                      <div className="rounded-full bg-[#8E79F4]/10 p-3 mb-3">
                        <Camera className="h-6 w-6 text-[#8E79F4] animate-pulse" />
                      </div>
                      <p className="text-sm font-medium text-white mb-1">Native Camera QR Scanner</p>
                      <p className="text-xs text-white/50 mb-4 max-w-xs">
                        Point your mobile camera at the device's pairing QR code to auto-register.
                      </p>
                      <Button 
                        onClick={handleScanQR}
                        disabled={loading}
                        className="clay-gradient-btn font-medium py-2 px-6 shadow-md"
                      >
                        {loading ? <Loader2 className="h-4 w-4 animate-spin mr-2" /> : <Camera className="h-4 w-4 mr-2" />}
                        Scan QR Code
                      </Button>
                    </div>
                  ) : (
                    <div className="flex flex-col items-center justify-center rounded-xl border border-white/5 bg-black/20 py-8 px-4 text-center">
                      <div className="rounded-full bg-[#8E79F4]/10 p-3 mb-3">
                        <Camera className="h-6 w-6 text-[#8E79F4]" />
                      </div>
                      <p className="text-sm font-medium text-white">Camera Scanner Integration</p>
                      <p className="text-xs text-white/50 mt-2 max-w-sm">
                        Camera scanner integration is active in the mobile app. For testing in this web browser, please use the <strong className="text-[#8E79F4] font-semibold">Manual Setup</strong> tab to pair devices.
                      </p>
                    </div>
                  )}
                </TabsContent>
                
                <TabsContent value="manual" className="mt-4 space-y-4">
                  <div className="space-y-2">
                    <Label htmlFor="hw_code" className="text-white">Hardware Code / MAC Address</Label>
                    <Input 
                      id="hw_code" 
                      placeholder="e.g. 24:0A:C4:A1:B2:C3" 
                      value={macAddress}
                      onChange={(e) => setMacAddress(e.target.value)}
                      className="font-mono text-sm bg-black/40 border-white/10 text-white" 
                    />
                    <p className="text-[10px] text-white/45">Found on the back sticker of the ZONE sensor component.</p>
                  </div>
                  <div className="space-y-2">
                    <Label htmlFor="pin" className="text-white">Pairing PIN</Label>
                    <Input 
                      id="pin" 
                      placeholder="6-digit PIN" 
                      value={pin}
                      onChange={(e) => setPin(e.target.value)}
                      className="font-mono text-sm bg-black/40 border-white/10 text-white" 
                    />
                    <p className="text-[10px] text-white/45">Unique PIN code provided by the manufacturer.</p>
                  </div>
                </TabsContent>
              </Tabs>
            </div>
 
            <DialogFooter className="border-t border-white/5 pt-4">
              <Button variant="outline" className="text-white border-white/10 hover:bg-white/5" onClick={() => setIsOpen(false)} disabled={loading}>Cancel</Button>
              <Button className="clay-gradient-btn" onClick={handleAdd} disabled={loading}>
                {loading && <Loader2 className="mr-2 h-4 w-4 animate-spin" />}
                Confirm Pairing
              </Button>
            </DialogFooter>
          </DialogContent>
        </Dialog>
      </div>
    </header>
  )
}
