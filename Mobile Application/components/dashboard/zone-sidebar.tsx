"use client"

import { useState } from "react"
import { useRouter } from "next/navigation"
import { signOut } from "firebase/auth"
import { auth } from "@/lib/firebase"
import { cn } from "@/lib/utils"
import { Bell, Plus, Radio, Volume2, AlertTriangle, Clock, Trash2, LogOut } from "lucide-react"
import { Button } from "@/components/ui/button"
import { ZoneLogo } from "@/components/ui/zone-logo"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog"
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
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from "@/components/ui/popover"

export interface ViolationEvent {
  id: string
  timestamp: string
  duration: string
  threshold: number
}

export interface Device {
  id: string
  name: string
  isOnline: boolean
  isEnabled: boolean
  isConnected?: boolean
  wifiControl?: string
  threshold?: number
  violations?: ViolationEvent[]
  savedSsid?: string | null
  savedPassword?: string | null
  lastSeen?: number | null
  savedNetworks?: Record<string, { ssid: string; password?: string | null }> | null
}

export interface Zone {
  id: string
  name: string
  location: string
  devices: Device[]
}

export interface NotificationAlert {
  id: string
  message: string
  timestamp: string
  isRead: boolean
}

interface ZoneSidebarProps {
  zones: Zone[]
  selectedZoneId: string
  onSelectZone: (zoneId: string) => void
  onAddZone?: (name: string, location: string) => void
  onRemoveZone?: (zoneId: string) => void
  notifications?: NotificationAlert[]
}

export function ZoneSidebar({
  zones,
  selectedZoneId,
  onSelectZone,
  onAddZone,
  onRemoveZone,
  notifications = [],
}: ZoneSidebarProps) {
  const router = useRouter()
  const [isAddZoneOpen, setIsAddZoneOpen] = useState(false)
  const [newZoneName, setNewZoneName] = useState("")
  const [newZoneLocation, setNewZoneLocation] = useState("")

  const handleLogout = async () => {
    try {
      await signOut(auth);
      router.push("/");
    } catch (error) {
      console.error("Logout failed", error);
    }
  };

  const handleAddZone = () => {
    if (newZoneName.trim() && onAddZone) {
      onAddZone(newZoneName, newZoneLocation || "New Location")
      setNewZoneName("")
      setNewZoneLocation("")
      setIsAddZoneOpen(false)
    }
  }

  const unreadCount = notifications.filter((n) => !n.isRead).length

  return (
    <aside className="flex h-full w-72 flex-col glass-sidebar z-10 relative">
      {/* Logo Area & Actions */}
      <div className="flex flex-col gap-4 border-b border-white/5 px-6 py-4">
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-3">
            <div className="shadow-md rounded-xl shrink-0">
              <ZoneLogo className="h-10 w-10" showRings={true} animate={false} />
            </div>
            <div>
              <h1 className="text-lg font-semibold tracking-tight text-white">
                Z.O.N.E.
              </h1>
              <p className="text-xs text-white/50">Admin Console</p>
            </div>
          </div>

          {/* Notifications Popover */}
          <Popover>
            <PopoverTrigger asChild>
              <Button variant="ghost" size="icon" className="relative h-8 w-8 rounded-full hover:bg-white/5">
                 <Bell className="h-4 w-4 text-white/50 hover:text-white" />
                 {unreadCount > 0 && (
                   <span className="absolute right-1 top-1 flex h-2 w-2 rounded-full bg-destructive" />
                 )}
              </Button>
            </PopoverTrigger>
            <PopoverContent className="w-80 border-white/10 bg-[#161224]/95 text-white p-0 shadow-2xl backdrop-blur-2xl" align="start">
               <div className="flex items-center justify-between border-b border-white/5 px-4 py-3">
                 <h4 className="text-sm font-semibold text-white">Notifications</h4>
                 {unreadCount > 0 && (
                   <span className="rounded-full bg-destructive/10 px-2 py-0.5 text-[10px] font-medium text-destructive">
                     {unreadCount} new
                   </span>
                 )}
               </div>
               <div className="max-h-80 overflow-y-auto">
                 {notifications.length === 0 ? (
                   <div className="px-4 py-6 text-center text-sm text-white/40">
                     No recent notifications
                   </div>
                 ) : (
                   <div className="flex flex-col">
                     {notifications.map((notif) => (
                       <div key={notif.id} className="flex flex-col gap-1 border-b border-white/5 px-4 py-3 last:border-0 hover:bg-white/5 transition-colors">
                         <span className="flex items-center gap-2 text-sm font-medium text-destructive">
                           <AlertTriangle className="h-3.5 w-3.5" />
                           {notif.message}
                         </span>
                         <span className="flex items-center gap-1 text-[10px] text-white/40 pl-5">
                           <Clock className="h-2.5 w-2.5" />
                           {new Intl.DateTimeFormat('en-US', { hour: 'numeric', minute: 'numeric', hour12: true }).format(new Date(notif.timestamp))}
                         </span>
                       </div>
                     ))}
                   </div>
                 )}
               </div>
            </PopoverContent>
          </Popover>
        </div>
        
        {/* Logout Button */}
        <AlertDialog>
          <AlertDialogTrigger asChild>
            <Button variant="outline" size="sm" className="w-full gap-2 border-white/10 bg-white/5 text-white hover:bg-white/10 hover:border-white/20 hover:text-white">
              <LogOut className="h-4 w-4" />
              Log Out
            </Button>
          </AlertDialogTrigger>
          <AlertDialogContent className="border-white/10 bg-[#161224]/95 text-white backdrop-blur-2xl">
            <AlertDialogHeader>
              <AlertDialogTitle className="text-white">Are you sure you want to log out?</AlertDialogTitle>
              <AlertDialogDescription className="text-white/60">
                You will be returned to the login page and securely signed out of your session.
              </AlertDialogDescription>
            </AlertDialogHeader>
            <AlertDialogFooter>
              <AlertDialogCancel className="text-white border-white/10 hover:bg-white/5">Cancel</AlertDialogCancel>
              <AlertDialogAction 
                onClick={handleLogout}
                className="clay-gradient-btn"
              >
                Log Out
              </AlertDialogAction>
            </AlertDialogFooter>
          </AlertDialogContent>
        </AlertDialog>
      </div>

      {/* Zone List */}
      <div className="flex-1 flex flex-col overflow-y-auto px-3 py-4">
        <p className="mb-3 px-3 text-[10px] font-bold uppercase tracking-wider text-white/40 font-mono shrink-0">
          Device Zones
        </p>
        <nav className="flex flex-col gap-1 flex-1">
          {zones.map((zone) => {
            const devices = zone.devices || []
            const onlineCount = devices.filter((d) => d.isOnline).length
            const totalCount = devices.length
            const allOnline = onlineCount > 0 && onlineCount === totalCount
            const someOnline = onlineCount > 0 && onlineCount < totalCount

            return (
              <div
                key={zone.id}
                onClick={() => onSelectZone(zone.id)}
                role="button"
                tabIndex={0}
                className={cn(
                  "group flex w-full items-center gap-3 rounded-lg px-3 py-3 text-left transition-all duration-200 shrink-0 cursor-pointer outline-none focus-visible:ring-1 focus-visible:ring-white/20",
                  selectedZoneId === zone.id
                    ? "bg-white/5 border border-white/10 text-white shadow-md"
                    : "hover:bg-white/2 hover:text-white border border-transparent text-white/50"
                )}
              >
                {/* Status Dot */}
                <span className="relative flex h-2.5 w-2.5">
                  {(allOnline || someOnline) && (
                    <span
                      className={cn(
                        "absolute inline-flex h-full w-full animate-ping rounded-full opacity-75",
                        allOnline ? "bg-[#8E79F4]" : "bg-warning"
                      )}
                    />
                  )}
                  <span
                    className={cn(
                      "relative inline-flex h-2.5 w-2.5 rounded-full",
                      allOnline ? "bg-[#8E79F4]" : someOnline ? "bg-warning" : "bg-destructive"
                    )}
                  />
                </span>

                <div className="flex-1 overflow-hidden">
                  <div className="flex items-center gap-2">
                    <p
                      className={cn(
                        "truncate text-sm font-medium transition-colors",
                        selectedZoneId === zone.id
                          ? "text-white"
                          : "group-hover:text-white"
                      )}
                    >
                      {zone.name}
                    </p>
                    <span className="shrink-0 rounded bg-white/10 px-1.5 py-0.5 text-[10px] font-medium tabular-nums text-white/70">
                      {onlineCount}/{totalCount}
                    </span>
                  </div>
                  <p className="truncate text-xs text-white/50">{zone.location}</p>
                </div>

                {selectedZoneId === zone.id && (
                  <Radio className="h-4 w-4 text-[#8E79F4] shrink-0" />
                )}
                {onRemoveZone && (
                  <div onClick={(e) => e.stopPropagation()}>
                    <AlertDialog>
                      <AlertDialogTrigger asChild>
                        <button 
                          className="p-1.5 ml-1 text-white/45 hover:text-red-500 hover:bg-white/5 rounded-md transition-all shrink-0"
                        >
                          <Trash2 className="h-3.5 w-3.5" />
                        </button>
                      </AlertDialogTrigger>
                      <AlertDialogContent className="border-white/10 bg-[#161224]/95 text-white backdrop-blur-2xl">
                        <AlertDialogHeader>
                          <AlertDialogTitle className="text-white">Are you sure you want to delete this zone?</AlertDialogTitle>
                          <AlertDialogDescription className="text-white/60">
                            This action cannot be undone. This will permanently remove the zone "{zone.name}" and all the devices added to that zone.
                          </AlertDialogDescription>
                        </AlertDialogHeader>
                        <AlertDialogFooter>
                          <AlertDialogCancel className="text-white border-white/10 hover:bg-white/5">Cancel</AlertDialogCancel>
                          <AlertDialogAction 
                            onClick={() => onRemoveZone(zone.id)}
                            className="bg-destructive text-destructive-foreground hover:bg-destructive/90"
                          >
                            Delete
                          </AlertDialogAction>
                        </AlertDialogFooter>
                      </AlertDialogContent>
                    </AlertDialog>
                  </div>
                )}
              </div>
            )
          })}
        </nav>

        {/* Add Zone Button inside the nav container at bottom */}
        <Dialog open={isAddZoneOpen} onOpenChange={setIsAddZoneOpen}>
          <DialogTrigger asChild>
            <Button variant="ghost" className="mt-4 w-full justify-start gap-2 text-white/60 hover:text-white hover:bg-white/5 shrink-0 border border-transparent hover:border-white/10 transition-all">
              <Plus className="h-4 w-4 text-white/80" />
              Add Zone
            </Button>
          </DialogTrigger>
          <DialogContent className="sm:max-w-[425px] border-white/10 bg-[#161224]/95 text-white backdrop-blur-2xl">
            <DialogHeader>
              <DialogTitle className="text-white border-b border-white/5 pb-4">Add New Zone</DialogTitle>
              <DialogDescription className="pt-2 text-white/50">
                Create a new monitoring zone to assign devices to.
              </DialogDescription>
            </DialogHeader>
            <div className="grid gap-4 py-4">
              <div className="grid grid-cols-4 items-center gap-4">
                <Label htmlFor="name" className="text-right text-white">
                  Name
                </Label>
                <Input
                  id="name"
                  value={newZoneName}
                  onChange={(e) => setNewZoneName(e.target.value)}
                  placeholder="e.g. Zone D"
                  className="col-span-3 bg-black/40 border-white/10 focus-visible:ring-white/20 text-white"
                />
              </div>
              <div className="grid grid-cols-4 items-center gap-4">
                <Label htmlFor="location" className="text-right text-white">
                  Description
                </Label>
                <Input
                  id="location"
                  value={newZoneLocation}
                  onChange={(e) => setNewZoneLocation(e.target.value)}
                  placeholder="e.g. Computer Lab"
                  className="col-span-3 bg-black/40 border-white/10 focus-visible:ring-white/20 text-white"
                />
              </div>
            </div>
            <DialogFooter>
              <Button variant="outline" className="text-white border-white/10 hover:bg-white/5" onClick={() => setIsAddZoneOpen(false)}>Cancel</Button>
              <Button onClick={handleAddZone} className="clay-gradient-btn">Add Zone</Button>
            </DialogFooter>
          </DialogContent>
        </Dialog>
      </div>

      {/* Footer */}
      <div className="border-t border-white/5 px-6 py-6 bg-black/10">
        <p className="text-xs text-white/40">
          Zonal Observer for Noise Evaluation
        </p>
        <p className="text-xs text-white/30">v2.1.0</p>
      </div>
    </aside>
  )
}
