import * as React from "react"

export function ZoneLogo({ 
  className = "h-8 w-8" 
}: { 
  className?: string
  showRings?: boolean
  animate?: boolean
}) {
  return (
    <div className={`relative ${className} flex items-center justify-center`}>
      <img
        src="/app_icon.png"
        className="w-full h-full object-contain"
        alt="Z.O.N.E. Logo"
      />
    </div>
  )
}
