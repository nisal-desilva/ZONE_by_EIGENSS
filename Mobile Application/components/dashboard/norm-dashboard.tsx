"use client"

import { useState, useEffect, useRef } from "react"
import { ZoneSidebar, type Zone, type NotificationAlert } from "./zone-sidebar"
import { ZoneHeader } from "./zone-header"
import { DeviceCard, type ViolationEvent } from "./device-card"
import { Menu, X, Loader2 } from "lucide-react"
import { Button } from "@/components/ui/button"
import { cn } from "@/lib/utils"
import { Capacitor } from "@capacitor/core"
import { LocalNotifications } from "@capacitor/local-notifications"
import { ZoneLogo } from "@/components/ui/zone-logo"
import { ref, onValue, set, remove, get, update } from "firebase/database"
import { database, auth } from "@/lib/firebase"

export function ZoneDashboard() {
  const uid = auth.currentUser?.uid;

  const [zones, setZones] = useState<Zone[]>([])
  const [selectedZoneId, setSelectedZoneId] = useState("")
  const [sidebarOpen, setSidebarOpen] = useState(false)
  const [notifications, setNotifications] = useState<NotificationAlert[]>([])
  const [isDataLoaded, setIsDataLoaded] = useState(false);

  const notifiedViolationsRef = useRef<Set<string>>(new Set())
  const isFirstLoadRef = useRef(true)

  // Request notifications permissions
  useEffect(() => {
    if (Capacitor.isNativePlatform()) {
      LocalNotifications.requestPermissions().then((result) => {
        console.log("Local notifications permission result:", result);
      });
    }
  }, []);

  // RTDB Sync, Mapping and Auto-Seed
  useEffect(() => {
    if (!uid) return;

    const userRef = ref(database, `users/${uid}`);
    const unsubscribe = onValue(userRef, (snapshot) => {
      const data = snapshot.val();
      
      // Try to clean up legacy root-level Zones folder programmatically
      remove(ref(database, "Zones")).catch(() => {});
      
      const hasLegacyData = data && (data.deviceThresholds || data.deviceViolations || Array.isArray(data.zones));

      if (!data || !data.zones || hasLegacyData) {
        if (hasLegacyData) {
          // Clean legacy structure first to prevent merge contamination
          remove(ref(database, `users/${uid}`)).catch((err) => {
            console.error("Failed to remove legacy user data:", err);
          });
        }
        setZones([]);
        setSelectedZoneId("");
        setIsDataLoaded(true);
        return;
      }

      // Automatically sync user's active devices to the master devices registry
      const rawZonesForSync = data.zones || {};
      for (const [zoneId, zoneVal] of Object.entries(rawZonesForSync)) {
        if (!zoneVal) continue;
        const val = zoneVal as any;
        const zoneDevices = val.devices || {};
        for (const [deviceId] of Object.entries(zoneDevices)) {
          // Verify if it is currently paired in registry, if not, update it
          get(ref(database, `devices/${deviceId}`)).then((devSnap) => {
            const devData = devSnap.val();
            if (!devData) {
              // Not registered at factory: remove from user dashboard
              remove(ref(database, `users/${uid}/zones/${zoneId}/devices/${deviceId}`));
            } else if (devData.paired && devData.ownerUid !== uid) {
              // Already paired to another user account: remove from this user's dashboard to prevent double-pairing
              remove(ref(database, `users/${uid}/zones/${zoneId}/devices/${deviceId}`));
            } else if (!devData.paired) {
              // Registered but unpaired: claim it for this user in the registry
              set(ref(database, `devices/${deviceId}`), {
                paired: true,
                ownerUid: uid
              });
            }
          });
        }
      }

      // 1. Data sweeper! Delete violations older than today
      let dirtyViolations = false;
      const startOfToday = new Date();
      startOfToday.setHours(0, 0, 0, 0);
      const todayTimeMs = startOfToday.getTime();

      const rawZones = data.zones || {};
      const updatedZonesForRTDB: any = {};
      const localMappedZones: Zone[] = [];

      for (const [zoneId, zoneVal] of Object.entries(rawZones)) {
        if (!zoneVal) continue;
        const val = zoneVal as any;
        const zoneDevices = val.devices || {};
        const updatedDevicesForRTDB: any = {};
        const localMappedDevices: Device[] = [];

        for (const [deviceId, deviceVal] of Object.entries(zoneDevices)) {
          if (!deviceVal) continue;
          const dVal = deviceVal as any;
          
          // Violations map/array parsing
          const rawViolations = dVal.violations || {};
          const mappedViolations: ViolationEvent[] = [];
          const cleanViolationsForRTDB: any = {};

          if (Array.isArray(rawViolations)) {
            rawViolations.forEach((v: any) => {
              if (v && new Date(v.timestamp).getTime() >= todayTimeMs) {
                mappedViolations.push(v);
                cleanViolationsForRTDB[v.id] = v;
              } else {
                dirtyViolations = true;
              }
            });
          } else {
            for (const [vId, vVal] of Object.entries(rawViolations)) {
              if (!vVal) continue;
              const viol = vVal as any;
              if (new Date(viol.timestamp).getTime() >= todayTimeMs) {
                mappedViolations.push({
                  id: vId,
                  ...viol
                });
                cleanViolationsForRTDB[vId] = viol;
              } else {
                dirtyViolations = true;
              }
            }
          }

          // Build mapped device for React state
          localMappedDevices.push({
            id: deviceId,
            name: dVal.name || "",
            isOnline: !!dVal.isOnline,
            isEnabled: !!dVal.isEnabled,
            isConnected: !!dVal.isConnected,
            threshold: dVal.threshold ?? 65,
            violations: mappedViolations,
            savedSsid: dVal.savedSsid || null,
            savedPassword: dVal.savedPassword || null,
            lastSeen: dVal.lastSeen ?? null,
            savedNetworks: dVal.savedNetworks || {}
          });

          // Build cleaned device for RTDB update (keeping only today's violations)
          updatedDevicesForRTDB[deviceId] = {
            name: dVal.name || "",
            isOnline: !!dVal.isOnline,
            isEnabled: !!dVal.isEnabled,
            isConnected: !!dVal.isConnected,
            threshold: dVal.threshold ?? 65,
            savedSsid: dVal.savedSsid || null,
            savedPassword: dVal.savedPassword || null,
            lastSeen: dVal.lastSeen ?? null,
            savedNetworks: dVal.savedNetworks || {}
          };
          if (Object.keys(cleanViolationsForRTDB).length > 0) {
            updatedDevicesForRTDB[deviceId].violations = cleanViolationsForRTDB;
          }
        }

        localMappedZones.push({
          id: zoneId,
          name: val.name || "",
          location: val.location || "",
          devices: localMappedDevices
        });

        updatedZonesForRTDB[zoneId] = {
          name: val.name || "",
          location: val.location || "",
        };
        if (Object.keys(updatedDevicesForRTDB).length > 0) {
          updatedZonesForRTDB[zoneId].devices = updatedDevicesForRTDB;
        }
      }

      // Clean up RTDB physically to save space if needed
      if (dirtyViolations) {
        set(ref(database, `users/${uid}/zones`), updatedZonesForRTDB);
      }

      setZones(localMappedZones);
      
      // Auto-select first zone if not selected
      setSelectedZoneId((current) => {
        if (!current && localMappedZones.length > 0) return localMappedZones[0].id;
        return current;
      });
      
      setIsDataLoaded(true);
    });

    return () => unsubscribe();
  }, [uid]);

  const selectedZone = zones.find((z) => z.id === selectedZoneId)
  const devicesToRender = selectedZone?.devices || [];

  // Notification Logic Engine & Native local notification triggers
  useEffect(() => {
    const alerts: NotificationAlert[] = []
    
    // Track new violations to notify
    const newViolationsToNotify: Array<{ deviceName: string; threshold: number; db: number }> = [];

    zones.forEach((zone) => {
      (zone.devices || []).forEach((device) => {
        const violations = device.violations || [];
        if (violations.length === 0) return;
        
        // 1. Process violations for native phone local notifications
        violations.forEach((violation) => {
          if (!notifiedViolationsRef.current.has(violation.id)) {
            // Add to notified set
            notifiedViolationsRef.current.add(violation.id);
            
            // Only trigger notifications after first load (to avoid old history spam)
            if (!isFirstLoadRef.current) {
              newViolationsToNotify.push({
                deviceName: device.name,
                threshold: violation.threshold,
                db: violation.dbValue ?? violation.threshold
              });
            }
          }
        });

        // 2. Frequency alert calculation (existing logic)
        const sorted = [...violations].sort((a, b) => new Date(b.timestamp).getTime() - new Date(a.timestamp).getTime())
        if (sorted.length >= 5) {
          for (let i = 0; i <= sorted.length - 5; i++) {
            const newest = new Date(sorted[i].timestamp).getTime()
            const fifth = new Date(sorted[i + 4].timestamp).getTime()
            
            if (newest - fifth <= 120000) {
              alerts.push({
                id: `alert-${device.id}-${sorted[i].id}`,
                message: `High violation frequency (>4 in 2 mins) detected on ${device.name}.`,
                timestamp: sorted[i].timestamp,
                isRead: false
              })
              break; 
            }
          }
        }
      });
    });

    // Mark first load complete
    if (zones.length > 0 && isFirstLoadRef.current) {
      isFirstLoadRef.current = false;
    }

    // Trigger local notifications for new violations
    if (newViolationsToNotify.length > 0 && Capacitor.isNativePlatform()) {
      newViolationsToNotify.forEach((viol) => {
        LocalNotifications.schedule({
          notifications: [
            {
              title: "Noise Limit Exceeded!",
              body: `${viol.deviceName} exceeded limit. Detected: ${viol.db} dB (Threshold: ${viol.threshold} dB).`,
              id: Math.floor(Math.random() * 100000),
              sound: "beep.wav",
              actionTypeId: "",
              extra: null
            }
          ]
        }).catch((err) => {
          console.error("Failed to schedule local notification:", err);
        });
      });
    }

    setNotifications(alerts)
  }, [zones])

  const handleClearDeviceViolations = (deviceId: string) => {
    if (!uid || !selectedZoneId) return;
    remove(ref(database, `users/${uid}/zones/${selectedZoneId}/devices/${deviceId}/violations`));
  }

  const handleDeviceThresholdChange = (deviceId: string, value: number) => {
    if (!uid || !selectedZoneId) return;
    set(ref(database, `users/${uid}/zones/${selectedZoneId}/devices/${deviceId}/threshold`), value);
  }

  const handleToggleDevice = (deviceId: string, enabled: boolean) => {
    if (!uid || !selectedZoneId) return;
    set(ref(database, `users/${uid}/zones/${selectedZoneId}/devices/${deviceId}/isEnabled`), enabled);
  }

  const handleToggleConnect = (deviceId: string, connected: boolean) => {
    if (!uid || !selectedZoneId) return;
    const deviceRef = ref(database, `users/${uid}/zones/${selectedZoneId}/devices/${deviceId}`);
    if (connected) {
      update(deviceRef, { 
        isConnected: true,
        wifiControl: ""
      });
    } else {
      update(deviceRef, {
        isConnected: false,
        wifiControl: "disconnect",
        isOnline: false
      });
    }
  }

  const handleForgetWifi = async (deviceId: string, ssid?: string) => {
    if (!uid || !selectedZoneId) return;
    const deviceRef = ref(database, `users/${uid}/zones/${selectedZoneId}/devices/${deviceId}`);
    
    if (ssid) {
      try {
        const snap = await get(deviceRef);
        const deviceData = snap.val() || {};
        const savedNetworks = deviceData.savedNetworks || {};
        const safeKey = ssid.replace(/[\.\$\#\[\]\/]/g, "_");
        if (savedNetworks[safeKey]) {
          delete savedNetworks[safeKey];
          
          const updates: any = { savedNetworks };
          
          // If the network we are forgetting is the currently active/connected one,
          // trigger a physical reset of the device as well.
          if (deviceData.savedSsid === ssid) {
            updates.savedSsid = null;
            updates.savedPassword = null;
            updates.wifiControl = "forget";
            updates.isConnected = false;
            updates.isOnline = false;
          }
          
          await update(deviceRef, updates);
        }
      } catch (err) {
        console.error("Failed to forget saved network:", err);
      }
    } else {
      update(deviceRef, {
        wifiControl: "forget",
        isConnected: false,
        isOnline: false
      });
    }
  }

  const handleSaveSsid = async (deviceId: string, ssid: string | null, password?: string | null) => {
    if (!uid || !selectedZoneId) return;
    const deviceRef = ref(database, `users/${uid}/zones/${selectedZoneId}/devices/${deviceId}`);
    
    if (ssid) {
      try {
        const snap = await get(deviceRef);
        const deviceData = snap.val() || {};
        const savedNetworks = deviceData.savedNetworks || {};
        const safeKey = ssid.replace(/[\.\$\#\[\]\/]/g, "_");
        
        savedNetworks[safeKey] = {
          ssid: ssid,
          password: password || null
        };
        
        // Limit to 4 saved networks max
        const keys = Object.keys(savedNetworks);
        if (keys.length > 4) {
          delete savedNetworks[keys[0]];
        }
        
        await update(deviceRef, { 
          savedNetworks,
          savedSsid: ssid,
          savedPassword: password || null
        });
      } catch (err) {
        console.error("Failed to save SSID:", err);
      }
    } else {
      update(deviceRef, {
        savedSsid: null,
        savedPassword: null
      });
    }
  }

  const handleSelectZone = (zoneId: string) => {
    setSelectedZoneId(zoneId)
    setSidebarOpen(false)
  }

  const handleAddZone = (name: string, location: string) => {
    if (!uid) return;
    const newId = `zone-${Date.now()}`
    set(ref(database, `users/${uid}/zones/${newId}`), {
      name,
      location
    });
    setSelectedZoneId(newId)
  }

  const handleAddDevice = async (name: string, mac: string, pin: string): Promise<{ success: boolean; error?: string }> => {
    if (!uid || !selectedZoneId) return { success: false, error: "Authentication required." };

    try {
      // Read the master registry value for this MAC address
      const snapshot = await get(ref(database, `devices/${mac}`));
      const registryData = snapshot.val();
      
      if (!registryData) {
        return { success: false, error: "Device code / MAC Address not registered at the factory." };
      }

      if (registryData.paired && registryData.ownerUid !== uid) {
        return { success: false, error: "This device is already paired to another user account." };
      }

      if (registryData.paired && registryData.ownerUid === uid) {
        return { success: false, error: "This device is already paired to your account." };
      }

      // Validate PIN code
      if (registryData.pin !== pin) {
        return { success: false, error: "Incorrect pairing PIN." };
      }

      // Success! Update the master registry
      await update(ref(database, `devices/${mac}`), {
        paired: true,
        ownerUid: uid,
        zoneId: selectedZoneId
      });

      // Add the device to the user's dashboard path
      await set(ref(database, `users/${uid}/zones/${selectedZoneId}/devices/${mac}`), {
        name,
        isOnline: false,
        isEnabled: true,
        threshold: 65
      });

      return { success: true };
    } catch (err: any) {
      console.error("Pairing query failed:", err);
      const errMsg = (err.message || "").toLowerCase();
      const errCode = (err.code || "").toLowerCase();
      if (errMsg.includes("permission") || errMsg.includes("denied") || errCode.includes("permission")) {
        return { success: false, error: "Database permission denied. Please verify your Firebase Security Rules." };
      }
      return { success: false, error: `Database error during pairing check: ${err.message || "Unknown error"}` };
    }
  }

  const handleRemoveZone = (zoneId: string) => {
    if (!uid) return;
    remove(ref(database, `users/${uid}/zones/${zoneId}`));
    if (selectedZoneId === zoneId) {
      setSelectedZoneId("");
    }
  }

  const handleRemoveDevice = async (deviceId: string) => {
    if (!uid || !selectedZoneId) return;
    try {
      // 1. Delete from user
      await remove(ref(database, `users/${uid}/zones/${selectedZoneId}/devices/${deviceId}`));
      
      // 2. Reset paired fields (preserving the pin!)
      await update(ref(database, `devices/${deviceId}`), {
        paired: false,
        ownerUid: null,
        zoneId: null
      });
    } catch (err) {
      console.error("Error removing device:", err);
    }
  }

  if (!isDataLoaded) {
    return (
      <div className="flex h-screen w-full items-center justify-center bg-[#06030A] relative overflow-hidden">
        {/* BACKGROUND GLOWING ORBS */}
        <div className="ambient-blob-1" />
        <div className="ambient-blob-2" />
        <div className="ambient-blob-3" />
        
        <div className="relative z-10 flex flex-col items-center text-center">
          <div className="mb-6 shadow-[0_0_50px_rgba(142,121,244,0.25)] rounded-3xl">
            <ZoneLogo className="h-28 w-28" showRings={true} animate={true} />
          </div>
          <h1 className="text-5xl font-black tracking-tight text-white mb-2">
            Z.O.N.E.
          </h1>
          <p className="text-xs uppercase tracking-[0.25em] text-[#8E79F4] font-bold">
            Syncing Cloud Console...
          </p>
        </div>
      </div>
    );
  }

  return (
    <div className="flex h-screen w-full bg-[#06030A] relative overflow-hidden">
      {/* BACKGROUND GLOWING ORBS */}
      <div className="ambient-blob-1" />
      <div className="ambient-blob-2" />
      <div className="ambient-blob-3" />

      {/* Mobile Sidebar Overlay */}
      <div
        className={cn(
          "fixed inset-0 z-40 bg-[#06030A]/85 backdrop-blur-sm transition-opacity lg:hidden",
          sidebarOpen ? "opacity-100" : "pointer-events-none opacity-0"
        )}
        onClick={() => setSidebarOpen(false)}
      />

      {/* Sidebar */}
      <div
        className={cn(
          "fixed inset-y-0 left-0 z-50 transition-transform duration-300 lg:static lg:translate-x-0 w-72",
          sidebarOpen ? "translate-x-0" : "-translate-x-full"
        )}
      >
        <ZoneSidebar
          zones={zones}
          selectedZoneId={selectedZoneId}
          onSelectZone={handleSelectZone}
          onAddZone={handleAddZone}
          onRemoveZone={handleRemoveZone}
          notifications={notifications}
        />
      </div>

      {/* Main Content */}
      <main className="flex min-w-0 flex-1 flex-col overflow-hidden z-10 relative">
        {/* Mobile Header */}
        <div className="flex items-center gap-3 border-b border-white/5 bg-white/2 backdrop-blur-xl px-4 py-3 lg:hidden">
          <Button
            variant="ghost"
            size="icon"
            onClick={() => setSidebarOpen(!sidebarOpen)}
            className="shrink-0 text-white"
          >
            {sidebarOpen ? <X className="h-5 w-5" /> : <Menu className="h-5 w-5" />}
          </Button>
          <span className="text-lg font-semibold text-white">Z.O.N.E. Admin</span>
        </div>

        {selectedZone ? (
          <>
            <ZoneHeader zone={selectedZone} onAddDevice={handleAddDevice} />

            <div className="flex-1 overflow-y-auto p-4 lg:p-10">
              <div className="mx-auto max-w-6xl">
                {/* Devices Section */}
                <section>
                  <h3 className="mb-4 text-[10px] font-bold uppercase tracking-wider text-white/40 font-mono">
                    Devices ({devicesToRender.length})
                  </h3>
                  
                  {devicesToRender.length === 0 ? (
                    <div className="flex flex-col items-center justify-center rounded-2xl border border-white/5 bg-white/1 py-24 text-center backdrop-blur-md">
                      <p className="text-white/40 mb-4 text-sm font-medium">No devices paired to this zone yet.</p>
                    </div>
                  ) : (
                    <div className="grid gap-6 sm:grid-cols-2 xl:grid-cols-3">
                      {devicesToRender.map((device) => (
                        <DeviceCard
                          key={device.id}
                          device={device}
                          onToggleDevice={handleToggleDevice}
                          onThresholdChange={handleDeviceThresholdChange}
                          onClearViolations={handleClearDeviceViolations}
                          onRemoveDevice={handleRemoveDevice}
                          onToggleConnect={handleToggleConnect}
                          onForgetWifi={handleForgetWifi}
                          onSaveSsid={handleSaveSsid}
                        />
                      ))}
                    </div>
                  )}
                </section>
              </div>
            </div>
          </>
        ) : (
          <div className="flex flex-1 flex-col items-center justify-center p-8">
            <h2 className="text-xl font-bold text-white mb-2">No Zones Available</h2>
            <p className="text-white/40 text-center text-sm">Please add or select a zone from the sidebar to view devices.</p>
          </div>
        )}
      </main>
    </div>
  )
}
