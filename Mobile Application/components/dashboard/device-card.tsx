"use client"

import { useState, useEffect } from "react"
import { cn } from "@/lib/utils"
import { Switch } from "@/components/ui/switch"
import { Slider } from "@/components/ui/slider"
import { Button } from "@/components/ui/button"
import { Capacitor } from "@capacitor/core"
import { BleClient } from "@capacitor-community/bluetooth-le"
import {
  AlertDialog,
  AlertDialogAction,
  AlertDialogCancel,
  AlertDialogContent,
  AlertDialogDescription,
  AlertDialogFooter,
  AlertDialogHeader,
  AlertDialogTitle,
  AlertDialogTrigger,
} from "@/components/ui/alert-dialog"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { Card, CardContent, CardHeader } from "@/components/ui/card"
import { Cpu, Wifi, WifiOff, AlertTriangle, Clock, Timer, Trash2, X, Bluetooth, Loader2, RefreshCw } from "lucide-react"
import type { Device } from "./zone-sidebar"
import { Tabs, TabsList, TabsTrigger, TabsContent } from "@/components/ui/tabs"

export interface ViolationEvent {
  id: string
  timestamp: string
  duration: string
  threshold: number
}

interface DeviceCardProps {
  device: Device
  onToggleDevice: (deviceId: string, enabled: boolean) => void
  onThresholdChange: (deviceId: string, value: number) => void
  onClearViolations: (deviceId: string) => void
  onRemoveDevice?: (deviceId: string) => void
  onToggleConnect: (deviceId: string, connected: boolean) => void
  onForgetWifi: (deviceId: string, ssid?: string) => void
  onSaveSsid?: (deviceId: string, ssid: string | null, password?: string | null) => void
}

export function DeviceCard({
  device,
  onToggleDevice,
  onThresholdChange,
  onClearViolations,
  onRemoveDevice,
  onToggleConnect,
  onForgetWifi,
  onSaveSsid,
}: DeviceCardProps) {
  const BLE_SERVICE_UUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
  const BLE_CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"

  const threshold = device.threshold ?? 65
  const violations = device.violations || []
  
  const [connectingBle, setConnectingBle] = useState(false)
  const [bleError, setBleError] = useState<string | null>(null)
  const [showProvisionDialog, setShowProvisionDialog] = useState(false)
  const [wifiSsid, setWifiSsid] = useState("")
  const [wifiPassword, setWifiPassword] = useState("")
  const [provisioningStatus, setProvisioningStatus] = useState("")
  const [forgettingWifi, setForgettingWifi] = useState(false)
  
  const [isConnecting, setIsConnecting] = useState(false)
  const [isDisconnecting, setIsDisconnecting] = useState(false)
  const [provisionAction, setProvisionAction] = useState<{ type: 'saved' | 'new'; ssid?: string; pass?: string } | null>(null)
  const [activeTab, setActiveTab] = useState("saved")
  const [connectedBleDevice, setConnectedBleDevice] = useState<any | null>(null)

  // Discovered BLE devices list
  const [showScanDialog, setShowScanDialog] = useState(false)
  const [discoveredDevices, setDiscoveredDevices] = useState<any[]>([])
  const [scanning, setScanning] = useState(false)
  const [scanModeType, setScanModeType] = useState<"connect" | "forget">("connect")

  // Set default tab when setup dialog is opened
  useEffect(() => {
    if (showProvisionDialog) {
      const hasSaved = device.savedNetworks && Object.keys(device.savedNetworks).length > 0;
      setActiveTab(hasSaved ? "saved" : "new")
    }
  }, [showProvisionDialog, device.savedNetworks])

  const [isOnline, setIsOnline] = useState(device.isOnline)

  useEffect(() => {
    const checkOnlineStatus = () => {
      if (!device.isOnline) {
        setIsOnline(false)
        return
      }
      if (!device.lastSeen) {
        setIsOnline(device.isOnline)
        return
      }
      const isAlive = (Date.now() - Number(device.lastSeen)) < 15000
      setIsOnline(isAlive)
    }

    checkOnlineStatus()
    const interval = setInterval(checkOnlineStatus, 5000)
    return () => clearInterval(interval)
  }, [device.isOnline, device.lastSeen])

  // Monitor connection transitions
  useEffect(() => {
    if (isOnline) {
      setIsConnecting(false)
    } else {
      setIsDisconnecting(false)
    }
  }, [isOnline])

  // Connection timeout handler
  useEffect(() => {
    let timer: NodeJS.Timeout
    if (isConnecting) {
      timer = setTimeout(() => {
        setIsConnecting(false)
        setBleError("Connection timed out. Please check if the device is powered on and within range.")
      }, 45000)
    }
    return () => clearTimeout(timer)
  }, [isConnecting])

  // Disconnection timeout handler
  useEffect(() => {
    let timer: NodeJS.Timeout
    if (isDisconnecting) {
      timer = setTimeout(() => {
        setIsDisconnecting(false)
        setBleError("Disconnection timed out.")
      }, 20000)
    }
    return () => clearTimeout(timer)
  }, [isDisconnecting])

  const getThresholdLabel = (val: number) => {
    if (val <= 50) return "Very Quiet"
    if (val <= 60) return "Quiet"
    if (val <= 70) return "Moderate"
    if (val <= 80) return "Loud"
    return "Very Loud"
  }

  const startDeviceScan = async (mode: "connect" | "forget") => {
    setScanModeType(mode)
    setDiscoveredDevices([])
    setBleError(null)
    setShowScanDialog(true)
    setScanning(true)
    setProvisioningStatus("Initializing Bluetooth...")

    try {
      await BleClient.initialize({ androidNeverForLocation: true })

      // Prompt user to enable Bluetooth on Android if disabled
      const isEnabledResult = await BleClient.isEnabled()
      if (!isEnabledResult.value) {
        if (Capacitor.getPlatform() === 'android') {
          try {
            await BleClient.requestEnable()
            
            // Poll for Bluetooth to actually turn on (up to 50 retries = 5 seconds)
            let retries = 50
            while (retries > 0) {
              const check = await BleClient.isEnabled()
              if (check.value) break
              await new Promise((resolve) => setTimeout(resolve, 100))
              retries--
            }
          } catch (e) {
            console.error("Failed to prompt Bluetooth activation:", e)
            throw new Error("Bluetooth activation request was denied or failed.")
          }
        } else {
          const isEnabledCheck = await BleClient.isEnabled()
          if (!isEnabledCheck.value) {
            throw new Error("Bluetooth is disabled. Please turn on Bluetooth to communicate.")
          }
        }
      }

      setProvisioningStatus("Scanning for nearby ZONE devices...")
      
      await BleClient.requestLEScan(
        {},
        (result) => {
          if (!result.device) return
          const devName = (result.device.name || result.localName || "").toUpperCase()
          
          if (devName.includes("ZONE")) {
            setDiscoveredDevices((prev) => {
              if (prev.some((d) => d.deviceId === result.device.deviceId)) {
                return prev
              }
              return [...prev, {
                deviceId: result.device.deviceId,
                name: result.device.name || result.localName || "ZONE-ESP32",
                rssi: result.rssi
              }]
            })
          }
        }
      )

      // Stop scanning after 10 seconds automatically
      setTimeout(async () => {
        try {
          await BleClient.stopLEScan()
        } catch (e) {}
        setScanning(false)
      }, 10000)

    } catch (err: any) {
      console.error("BLE scan failed:", err)
      setBleError(`Bluetooth error: ${err.message || err}`)
      setScanning(false)
      setShowScanDialog(false)
    }
  }

  const handleSelectDevice = async (selectedDevice: any) => {
    setScanning(false)
    try {
      await BleClient.stopLEScan()
    } catch (e) {}
    
    setShowScanDialog(false)
    setConnectingBle(true)
    setProvisioningStatus(`Connecting to ${selectedDevice.name}...`)

    try {
      await BleClient.connect(selectedDevice.deviceId, (disconnectedId) => {
        console.log(`BLE disconnected: ${disconnectedId}`)
        setConnectingBle(false)
        setConnectedBleDevice(null)
      })

      try {
        await BleClient.requestMtu(selectedDevice.deviceId, 512)
      } catch (mtuErr) {
        console.warn("Failed to request MTU:", mtuErr)
      }

      if (scanModeType === "connect") {
        setConnectedBleDevice(selectedDevice)
        setConnectingBle(false)
        setShowProvisionDialog(true)
      } else {
        // Forget mode
        setProvisioningStatus("Clearing device Wi-Fi credentials...")
        const encoder = new TextEncoder()
        const dataView = new DataView(encoder.encode("FORGET").buffer)

        await BleClient.write(
          selectedDevice.deviceId,
          BLE_SERVICE_UUID,
          BLE_CHARACTERISTIC_UUID,
          dataView
        )

        onForgetWifi(device.id)
        try {
          await BleClient.disconnect(selectedDevice.deviceId)
        } catch (e) {}
        setConnectingBle(false)
      }
    } catch (connErr: any) {
      console.error("BLE device action failed:", connErr)
      setBleError(`Action failed: ${connErr.message || connErr}`)
      setConnectingBle(false)
      setConnectedBleDevice(null)
    }
  }

  const sendWifiCredentialsToDevice = async (ssid: string, password?: string | null) => {
    if (!connectedBleDevice) {
      setBleError("No Bluetooth device connected.")
      return
    }

    setShowProvisionDialog(false)
    setIsConnecting(true)
    setProvisioningStatus(`Sending credentials for "${ssid}"...`)

    try {
      const encoder = new TextEncoder()
      const payload = `${ssid},${password || ""}`
      
      if (onSaveSsid && ssid) {
        onSaveSsid(device.id, ssid, password || null)
      }

      const dataView = new DataView(encoder.encode(payload).buffer)
      await BleClient.write(
        connectedBleDevice.deviceId,
        BLE_SERVICE_UUID,
        BLE_CHARACTERISTIC_UUID,
        dataView
      )

      onToggleConnect(device.id, true)
      setProvisioningStatus("Waiting for device to connect to Wi-Fi...")

      try {
        await BleClient.disconnect(connectedBleDevice.deviceId)
      } catch (e) {}
    } catch (err: any) {
      console.error("BLE write failed:", err)
      setBleError(`Failed to configure Wi-Fi: ${err.message || err}`)
      setIsConnecting(false)
    } finally {
      setConnectedBleDevice(null)
    }
  }

  const handleConnectClick = async () => {
    if (isOnline) {
      setIsDisconnecting(true)
      onToggleConnect(device.id, false)
      return
    }

    setBleError(null)
    setWifiSsid(device.savedSsid || "")
    setWifiPassword("")
    setProvisionAction(null)
    setConnectedBleDevice(null)

    if (!Capacitor.isNativePlatform()) {
      setBleError("Bluetooth configuration is only supported on mobile devices. For browser testing, please ensure the device is online.")
      return
    }

    await startDeviceScan("connect")
  }

  const handleUseSavedNetwork = async (ssid: string, password?: string | null) => {
    if (!ssid) return
    await sendWifiCredentialsToDevice(ssid, password)
  }

  const handleConfigureNewNetwork = async () => {
    if (!wifiSsid) {
      setBleError("Please enter a Wi-Fi SSID.")
      return
    }
    await sendWifiCredentialsToDevice(wifiSsid, wifiPassword)
  }

  const handleForgetSavedNetwork = (ssid: string) => {
    onForgetWifi(device.id, ssid)
    
    // Switch tab to "new" if no other saved networks remain
    const remainingKeys = device.savedNetworks
      ? Object.keys(device.savedNetworks).filter((k) => k !== ssid.replace(/[\.\$\#\[\]\/]/g, "_"))
      : []
    if (remainingKeys.length === 0) {
      setActiveTab("new")
    }
  }

  const isActive = isOnline && device.isEnabled

  return (
    <Card
      className={cn(
        "relative flex flex-col overflow-hidden transition-all duration-300 glass-card p-3.5 flex flex-col justify-between min-h-[305px] rounded-[20px]"
      )}
    >
      <CardHeader className="p-0 pb-2 border-b border-white/5">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-2.5">
            <div className="h-10 w-10 rounded-2xl bg-gradient-to-br from-[#8E79F4] to-[#5856D6] flex items-center justify-center border border-white/10 shadow-md">
              <Cpu className="h-5 w-5 text-white" />
            </div>
            <div className="flex flex-col gap-0.5">
              <div className="flex items-center gap-1.5 font-sans">
                <h3 className="font-bold text-sm text-white leading-none">{device.name}</h3>
                <span className="relative flex h-2 w-2">
                  {isOnline && (
                    <span className="radar-ring" style={{ backgroundColor: "#22C55E" }} />
                  )}
                  <span
                    className={cn(
                      "relative inline-flex h-2 w-2 rounded-full",
                      isOnline ? "bg-green-500 shadow-[0_0_8px_rgba(34,197,94,0.5)]" : "bg-red-500 shadow-md"
                    )}
                  />
                </span>
              </div>
              <p className="flex items-center gap-1 text-[11px] text-white/50 font-semibold mt-0.5">
                {isOnline ? (
                  <>
                    <Wifi className="h-3 w-3" />
                    Online
                  </>
                ) : (
                  <>
                    <WifiOff className="h-3 w-3" />
                    Offline
                  </>
                )}
              </p>
              <p className="text-[9px] font-mono text-white/40 block leading-none mt-0.5 uppercase tracking-wide">
                MAC: {device.id.length === 12 ? device.id.match(/.{1,2}/g)?.join(":") : device.id}
              </p>
            </div>
          </div>

          <div className="flex items-center gap-2">
            <span className="text-[11px] text-white/50 font-bold">
              {device.isEnabled ? "On" : "Off"}
            </span>
            <Switch
              checked={device.isEnabled}
              onCheckedChange={(checked) => onToggleDevice(device.id, checked)}
              disabled={!isOnline}
              className="data-[state=checked]:bg-[#5856D6]"
            />
            {onRemoveDevice && (
              <AlertDialog>
                <AlertDialogTrigger asChild>
                  <Button 
                    variant="ghost" 
                    size="icon" 
                    className="p-1.5 hover:bg-white/5 rounded-lg text-white/40 hover:text-red-500 ml-1 transition-colors"
                  >
                    <X className="h-3.5 w-3.5" />
                  </Button>
                </AlertDialogTrigger>
                <AlertDialogContent className="border-white/10 bg-[#161224]/95 text-white backdrop-blur-2xl">
                  <AlertDialogHeader>
                    <AlertDialogTitle className="text-white">Are you sure you want to delete this device?</AlertDialogTitle>
                    <AlertDialogDescription className="text-white/60">
                      This action cannot be undone. This will permanently remove "{device.name}" from the system.
                    </AlertDialogDescription>
                  </AlertDialogHeader>
                  <AlertDialogFooter>
                    <AlertDialogCancel className="text-white border-white/10 hover:bg-white/5">Cancel</AlertDialogCancel>
                    <AlertDialogAction 
                      onClick={() => onRemoveDevice(device.id)}
                      className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
                    >
                      Delete
                    </AlertDialogAction>
                  </AlertDialogFooter>
                </AlertDialogContent>
              </AlertDialog>
            )}
          </div>
        </div>
      </CardHeader>

      <CardContent className="p-0 flex flex-1 flex-col justify-between mt-2.5">
        {/* Connect / Disconnect Action Button */}
        <div className="flex flex-col gap-2 mb-2.5">
          {bleError && (
            <div className="flex items-center gap-1.5 rounded bg-destructive/10 border border-destructive/20 p-2.5 text-[11px] text-destructive leading-tight">
              <AlertTriangle className="h-3.5 w-3.5 shrink-0" />
              <span>{bleError}</span>
            </div>
          )}

          {(() => {
            const showLoading = isConnecting || isDisconnecting || connectingBle;
            const loadingText = isDisconnecting 
              ? "Disconnecting..." 
              : isConnecting 
                ? (provisioningStatus || "Connecting to Wi-Fi...") 
                : (provisioningStatus || "Connecting via BLE...");
            
            return (
              <Button
                onClick={handleConnectClick}
                disabled={showLoading || forgettingWifi}
                className={cn(
                  "w-full font-bold py-2 text-xs flex items-center justify-center gap-2 rounded-xl transition-all border-0",
                  isOnline
                    ? "bg-white/5 border border-white/10 hover:bg-white/10 text-white"
                    : "clay-gradient-btn"
                )}
              >
                {showLoading ? (
                  <>
                    <Loader2 className="h-3.5 w-3.5 animate-spin mr-1 text-current" />
                    {loadingText}
                  </>
                ) : isOnline ? (
                  <>
                    <WifiOff className="h-3.5 w-3.5" />
                    Disconnect Device
                  </>
                ) : (
                  <>
                    <Wifi className="h-3.5 w-3.5" />
                    Connect Device
                  </>
                )}
              </Button>
            );
          })()}
        </div>

        {/* Threshold Control */}
        <div className={cn("space-y-1.5", !isOnline && "opacity-45 pointer-events-none")}>
          <div className="flex justify-between items-end">
            <div>
              <p className="text-[9px] font-bold uppercase tracking-wider text-white/40 font-mono">
                Threshold
              </p>
              <div className="mt-0.5 flex items-center gap-2">
                <span className="text-4xl font-display font-extrabold tracking-tight text-white leading-none">
                  {threshold}
                </span>
                <span className="text-xs text-white/50 font-mono font-bold">dB</span>
                {/* Loop Equalizer Soundwave */}
                {isActive && (
                  <div className="flex items-end gap-[2px] h-5 pb-[3px] ml-1">
                    <div className="w-[3px] bg-[#8E79F4] rounded-full eq-bar-1"></div>
                    <div className="w-[3px] bg-[#5856D6] rounded-full eq-bar-2"></div>
                    <div className="w-[3px] bg-white/40 rounded-full eq-bar-3"></div>
                  </div>
                )}
              </div>
            </div>
            <span
              className={cn(
                "text-xs font-bold uppercase tracking-wide",
                isActive ? "text-[#8E79F4]" : "text-white/40"
              )}
            >
              {getThresholdLabel(threshold)}
            </span>
          </div>

          <div className="space-y-1">
            <Slider
              value={[threshold]}
              onValueChange={(vals) => onThresholdChange(device.id, vals[0])}
              min={40}
              max={90}
              step={1}
              disabled={!isActive}
              className="w-full appearance-none outline-none [&_[role=slider]]:bg-white [&_[role=slider]]:shadow-lg [&_[role=slider]]:border-[#5856D6]/40 [&_[role=slider]]:h-5 [&_[role=slider]]:w-5 [&_[role=slider]]:rounded-full [&_.relative]:bg-[#5856D6]"
            />
            <div className="flex justify-between text-[9px] text-white/45 font-semibold font-mono">
              <span>40 dB</span>
              <span>90 dB</span>
            </div>
          </div>
        </div>

        {/* Violations Section */}
        <div className={cn("mt-2.5 border-0 rounded-xl glass-neumorphic-inset p-3 flex flex-col justify-between shadow-inner", !isOnline && "opacity-45")}>
          <div className="mb-1 flex items-center justify-between">
            <div className="flex items-center gap-1.5">
              <AlertTriangle className="h-4 w-4 text-[#8E79F4]" />
              <span className="text-xs font-bold text-[#8E79F4]">
                Violations ({violations?.length ?? 0})
              </span>
            </div>
            {(violations?.length ?? 0) > 0 && (
              <Button
                variant="ghost"
                size="sm"
                onClick={() => onClearViolations(device.id)}
                className="h-5 gap-1 px-1.5 text-[9px] text-white/40 hover:text-red-500 hover:bg-white/5 rounded-md"
              >
                <Trash2 className="h-2.5 w-2.5" />
                Clear
              </Button>
            )}
          </div>

          {(violations?.length ?? 0) === 0 ? (
            <div className="flex flex-1 items-center justify-center py-1">
              <p className="text-[11px] font-mono text-white/40">No violations recorded</p>
            </div>
          ) : (
            <div className="max-h-20 space-y-1 overflow-y-auto pr-2">
              {(violations ?? []).slice(0, 5).map((violation, index) => (
                <div
                  key={violation.id}
                  className="flex items-center justify-between border-b border-white/5 py-1.5 last:border-0 text-white/80"
                >
                  <div className="flex items-center gap-1">
                    <span className="text-[11px] font-medium text-white">
                      {violation.threshold} dB exceeded
                    </span>
                  </div>
                  <div className="flex items-center gap-2 text-[9px] text-white/50">
                    <span className="flex items-center gap-1">
                      <Clock className="h-2.5 w-2.5" />
                      {new Intl.DateTimeFormat('en-US', { hour: 'numeric', minute: 'numeric', hour12: true }).format(new Date(violation.timestamp))}
                    </span>
                  </div>
                </div>
              ))}
              {(violations?.length ?? 0) > 5 && (
                <p className="py-0.5 text-center text-[9px] text-white/40">
                  +{(violations?.length ?? 0) - 5} more
                </p>
              )}
            </div>
          )}
        </div>

        {/* Bottom Row */}
        <div className="flex items-center justify-between border-t border-white/5 pt-2 mt-2">
          <span></span>
          <span className="text-[9px] text-white/30 uppercase font-bold tracking-wider font-mono">
            {isOnline ? "Control Active" : "Control Locked"}
          </span>
        </div>
      </CardContent>

      {/* Wi-Fi Provisioning Dialog */}
      <Dialog open={showProvisionDialog} onOpenChange={(open) => {
        if (!open && connectedBleDevice) {
          try {
            BleClient.disconnect(connectedBleDevice.deviceId)
          } catch (e) {}
          setConnectedBleDevice(null)
        }
        setShowProvisionDialog(open)
      }}>
        <DialogContent className="sm:max-w-[400px] border-white/10 bg-[#161224]/95 text-white backdrop-blur-2xl">
          <DialogHeader>
            <DialogTitle className="text-white">Wi-Fi Connection Setup</DialogTitle>
            <DialogDescription className="pt-2 text-xs text-white/50">
              Configure the Wi-Fi connection for your hardware {device.name}.
            </DialogDescription>
          </DialogHeader>

          <div className="py-4">
            <Tabs value={activeTab} onValueChange={setActiveTab} className="w-full">
              <TabsList className="grid w-full grid-cols-2 bg-black/40 border border-white/10 p-1">
                <TabsTrigger 
                  value="saved" 
                  disabled={!device.savedNetworks || Object.keys(device.savedNetworks).length === 0} 
                  className="text-xs data-[state=active]:bg-white/10 data-[state=active]:text-white"
                >
                  Saved Networks
                </TabsTrigger>
                <TabsTrigger value="new" className="text-xs data-[state=active]:bg-white/10 data-[state=active]:text-white">
                  New Network
                </TabsTrigger>
              </TabsList>
              
              <TabsContent value="saved" className="space-y-3 pt-4 text-left">
                {device.savedNetworks && Object.keys(device.savedNetworks).length > 0 ? (
                  <div className="space-y-2 max-h-[220px] overflow-y-auto pr-1">
                    {Object.values(device.savedNetworks).map((net) => {
                      const isActiveNet = device.savedSsid === net.ssid;
                      return (
                        <div
                          key={net.ssid}
                          className={cn(
                            "flex items-center justify-between rounded-lg border p-3 text-xs transition-all",
                            isActiveNet
                              ? "border-[#8E79F4]/40 bg-[#8E79F4]/10 shadow-md"
                              : "border-white/10 bg-black/20 hover:border-white/20"
                          )}
                        >
                          <div className="flex flex-col gap-0.5 min-w-0 mr-2">
                            <span className="font-semibold text-white truncate max-w-[150px]">
                              {net.ssid}
                            </span>
                            {isActiveNet && (
                              <span className="text-[10px] text-[#8E79F4] font-medium flex items-center gap-1">
                                <span className="h-1.5 w-1.5 rounded-full bg-[#8E79F4] animate-pulse" />
                                Active Network
                              </span>
                            )}
                          </div>
                          <div className="flex items-center gap-2 shrink-0">
                            <Button
                              onClick={() => handleUseSavedNetwork(net.ssid, net.password)}
                              size="sm"
                              className="h-7 clay-gradient-btn text-[11px] px-2.5 font-semibold flex items-center gap-1"
                            >
                              <Wifi className="h-3 w-3" />
                              Connect
                            </Button>
                            <Button
                              variant="ghost"
                              onClick={() => handleForgetSavedNetwork(net.ssid)}
                              size="icon"
                              className="h-7 w-7 text-white/40 hover:text-red-500 hover:bg-white/5 transition-colors"
                            >
                              <Trash2 className="h-3.5 w-3.5" />
                            </Button>
                          </div>
                        </div>
                      );
                    })}
                  </div>
                ) : (
                  <p className="text-xs text-white/40 text-center py-4">No saved network configuration found.</p>
                )}
              </TabsContent>

              <TabsContent value="new" className="space-y-4 pt-4 text-left">
                <div className="space-y-3">
                  <div className="space-y-1.5">
                    <Label htmlFor="card_wifi_ssid" className="text-xs text-white">Wi-Fi SSID (Name)</Label>
                    <Input
                      id="card_wifi_ssid"
                      placeholder="Enter Wi-Fi network name"
                      value={wifiSsid}
                      onChange={(e) => setWifiSsid(e.target.value)}
                      className="text-xs bg-black/40 border-white/10 text-white focus-visible:ring-white/20 py-1.5 h-8"
                    />
                  </div>

                  <div className="space-y-1.5">
                    <Label htmlFor="card_wifi_pass" className="text-xs text-white">Wi-Fi Password</Label>
                    <Input
                      id="card_wifi_pass"
                      type="password"
                      placeholder="Enter Wi-Fi password"
                      value={wifiPassword}
                      onChange={(e) => setWifiPassword(e.target.value)}
                      className="text-xs bg-black/40 border-white/10 text-white focus-visible:ring-white/20 py-1.5 h-8"
                    />
                  </div>

                  <Button
                    onClick={handleConfigureNewNetwork}
                    disabled={!wifiSsid}
                    className="w-full clay-gradient-btn text-xs py-1.5 h-9 font-semibold mt-2 flex items-center justify-center gap-2"
                  >
                    <Bluetooth className="h-3.5 w-3.5" />
                    Configure & Connect
                  </Button>
                </div>
              </TabsContent>
            </Tabs>
          </div>
        </DialogContent>
      </Dialog>

      {/* Bluetooth Device Selection Dialog */}
      <Dialog open={showScanDialog} onOpenChange={(open) => {
        if (!open) {
          try {
            BleClient.stopLEScan()
          } catch (e) {}
          setScanning(false)
        }
        setShowScanDialog(open)
      }}>
        <DialogContent className="sm:max-w-[400px] border-white/10 bg-[#161224]/95 text-white backdrop-blur-2xl">
          <DialogHeader>
            <DialogTitle className="text-white">Select Bluetooth Device</DialogTitle>
            <DialogDescription className="pt-2 text-xs text-white/50">
              Connect to your physical ZONE-ESP32 device nearby.
            </DialogDescription>
          </DialogHeader>

          <div className="py-4 space-y-4">
            {scanning && (
              <div className="flex items-center justify-center gap-2 rounded bg-[#8E79F4]/10 border border-[#8E79F4]/20 p-3 text-xs text-[#8E79F4]">
                <Loader2 className="h-4 w-4 animate-spin shrink-0" />
                <span>Scanning for nearby ZONE devices...</span>
              </div>
            )}

            {!scanning && discoveredDevices.length === 0 && (
              <div className="text-center py-6 space-y-3">
                <p className="text-xs text-white/40">No ZONE devices found nearby.</p>
                <Button
                  onClick={() => startDeviceScan(scanModeType)}
                  className="mx-auto flex items-center gap-1.5 bg-white/5 text-white hover:bg-white/10 border border-white/10 text-xs py-1 h-8 px-4"
                >
                  <RefreshCw className="h-3 w-3" />
                  Scan Again
                </Button>
              </div>
            )}

            {discoveredDevices.length > 0 && (
              <div className="space-y-2">
                <p className="text-[10px] uppercase tracking-wider text-white/40 font-semibold font-mono">Discovered Devices ({discoveredDevices.length})</p>
                <div className="max-h-48 overflow-y-auto space-y-2 pr-1">
                  {discoveredDevices.map((dev) => (
                    <button
                      key={dev.deviceId}
                      onClick={() => handleSelectDevice(dev)}
                      className={cn(
                        "w-full text-left flex items-center justify-between rounded-lg border border-white/10 bg-black/20 p-3",
                        "hover:border-[#8E79F4]/55 hover:bg-white/5 transition-all cursor-pointer text-white"
                      )}
                    >
                      <div className="flex items-center gap-2.5">
                        <Bluetooth className="h-4 w-4 text-[#8E79F4]" />
                        <div>
                          <p className="text-xs font-semibold text-white">{dev.name}</p>
                          <p className="text-[9px] font-mono text-white/40 uppercase">{dev.deviceId}</p>
                        </div>
                      </div>
                      <div className="flex items-center gap-2">
                        {dev.rssi !== undefined && (
                          <span className="text-[10px] font-mono text-white/40">{dev.rssi} dBm</span>
                        )}
                        <span className="text-[10px] font-semibold text-[#8E79F4]">Select</span>
                      </div>
                    </button>
                  ))}
                </div>
              </div>
            )}
          </div>
          <DialogFooter className="border-t border-white/5 pt-4">
            <Button
              variant="outline"
              onClick={() => {
                try {
                  BleClient.stopLEScan()
                } catch (e) {}
                setScanning(false)
                setShowScanDialog(false)
              }}
              className="text-white border-white/10 hover:bg-white/5 text-xs h-8"
            >
              Cancel
            </Button>
            {scanning && (
              <Button
                variant="ghost"
                onClick={async () => {
                  try {
                    await BleClient.stopLEScan()
                  } catch (e) {}
                  setScanning(false)
                }}
                className="text-white/45 hover:text-white text-xs h-8"
              >
                Stop Scan
              </Button>
            )}
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </Card>
  )
}
