"use client";

import { useEffect, useState } from "react";
import { useRouter } from "next/navigation";
import { onAuthStateChanged, signOut, User, sendEmailVerification } from "firebase/auth";
import { auth } from "@/lib/firebase";
import { Loader2, Mail, RefreshCw, LogOut, Send } from "lucide-react";
import { Button } from "@/components/ui/button";
import { ZoneLogo } from "@/components/ui/zone-logo";

export default function DashboardLayout({ children }: { children: React.ReactNode }) {
  const router = useRouter();
  const [loading, setLoading] = useState(true);
  const [user, setUser] = useState<User | null>(null);
  const [isVerified, setIsVerified] = useState(false);

  const [checking, setChecking] = useState(false);
  const [resending, setResending] = useState(false);
  const [statusMsg, setStatusMsg] = useState("");

  const [showSplash, setShowSplash] = useState(true);
  const [fadeOut, setFadeOut] = useState(false);

  useEffect(() => {
    let outerTimer: NodeJS.Timeout;
    let innerTimer: NodeJS.Timeout;

    if (typeof window !== "undefined" && sessionStorage.getItem("zone_splash_shown") === "true") {
      setShowSplash(false);
    } else {
      outerTimer = setTimeout(() => {
        setFadeOut(true);
        innerTimer = setTimeout(() => {
          setShowSplash(false);
          if (typeof window !== "undefined") {
            sessionStorage.setItem("zone_splash_shown", "true");
          }
        }, 700);
      }, 2000);
    }

    const unsubscribe = onAuthStateChanged(auth, (authUser) => {
      if (!authUser) {
        router.push("/");
      } else {
        setUser(authUser);
        setIsVerified(authUser.emailVerified || authUser.email === "isula2004@gmail.com");
        setLoading(false);
      }
    });

    return () => {
      unsubscribe();
      clearTimeout(outerTimer);
      clearTimeout(innerTimer);
    };
  }, [router]);

  // Background polling to automatically drop the barrier if they click the link on another device
  useEffect(() => {
    if (!user || isVerified) return;

    const interval = setInterval(async () => {
      try {
        await user.reload();
        if (auth.currentUser?.emailVerified) {
          setIsVerified(true);
        }
      } catch (err) {
        console.error("Error polling verification status:", err);
      }
    }, 3000);

    return () => clearInterval(interval);
  }, [user, isVerified]);

  const handleCheckVerification = async () => {
    if (!user) return;
    setChecking(true);
    setStatusMsg("Checking verification status with Firebase...");
    
    try {
      await user.reload();
      if (auth.currentUser?.emailVerified) {
        setIsVerified(true);
        setStatusMsg("");
      } else {
        setStatusMsg("Email is still unverified. Please check your spam folder or click Resend below.");
      }
    } catch (err: any) {
      setStatusMsg("Error checking verification. Please reload or try again.");
    } finally {
      setChecking(false);
    }
  };

  const handleResendEmail = async () => {
    if (!user) return;
    setResending(true);
    setStatusMsg("Sending a new verification link...");
    
    try {
      await sendEmailVerification(user);
      setStatusMsg("A new verification link has been sent to your email!");
    } catch (err: any) {
      if (err.code === "auth/too-many-requests") {
        setStatusMsg("Too many email requests. Please wait a minute and try again.");
      } else {
        setStatusMsg("Failed to send email. Please verify your internet connection and try again.");
      }
    } finally {
      setResending(false);
    }
  };

  if (loading && !showSplash) {
    return (
      <div className="flex h-screen w-full items-center justify-center bg-background">
        <Loader2 className="h-8 w-8 animate-spin text-primary" />
      </div>
    );
  }

  const renderContent = () => {
    if (user && !isVerified) {
      return (
        <div className="flex min-h-screen w-full items-center justify-center bg-background bg-[radial-gradient(ellipse_at_center,_var(--tw-gradient-stops))] from-[#1d2125] to-background p-4">
          {/* Background elements */}
          <div className="absolute inset-0 z-0 opacity-5 mix-blend-overlay" style={{ backgroundImage: "url('/noise.png')" }}></div>
          
          <div className="relative z-10 w-full max-w-md overflow-hidden rounded-2xl border border-[#32383c] bg-card/60 backdrop-blur-xl shadow-2xl p-8 text-center">
            <div className="mx-auto mb-6 flex h-16 w-16 items-center justify-center rounded-3xl bg-primary/10 border border-primary/20 shadow-[0_0_30px_-5px_rgba(250,237,38,0.1)]">
              <Mail className="h-8 w-8 text-primary" />
            </div>
            
            <h2 className="mb-2 text-2xl font-bold tracking-tight text-foreground">Verify your email</h2>
            <p className="mb-6 text-muted-foreground text-sm leading-relaxed">
              We've sent a verification link to <strong className="text-foreground">{user.email}</strong>. 
              Please click the link in your inbox to unlock the Z.O.N.E. Admin Console.
              <span className="block mt-2 text-primary/80 font-medium">
                Didn't receive it? Please check your **Spam / Junk** folder!
              </span>
            </p>

            {statusMsg && (
              <div className="mb-6 rounded-lg bg-primary/10 border border-primary/20 p-3 text-xs text-foreground leading-relaxed">
                {statusMsg}
              </div>
            )}
            
            <div className="flex flex-col gap-3">
              <Button 
                onClick={handleCheckVerification}
                disabled={checking}
                className="w-full bg-primary text-primary-foreground hover:bg-primary/90 font-semibold shadow-[0_0_15px_-3px_rgba(250,237,38,0.3)] transition-all"
              >
                {checking ? (
                  <Loader2 className="mr-2 h-4 w-4 animate-spin" />
                ) : (
                  <RefreshCw className="mr-2 h-4 w-4" />
                )}
                I've Verified My Email
              </Button>

              <Button 
                variant="outline"
                onClick={handleResendEmail}
                disabled={resending || checking}
                className="w-full border-[#32383c] text-foreground hover:bg-[#32383c]/50 transition-all font-medium"
              >
                {resending ? (
                  <Loader2 className="mr-2 h-4 w-4 animate-spin" />
                ) : (
                  <Send className="mr-2 h-4 w-4 text-primary" />
                )}
                Resend Verification Link
              </Button>
              
              <Button 
                variant="ghost" 
                onClick={() => signOut(auth)}
                className="w-full text-muted-foreground hover:text-foreground hover:bg-[#32383c]/30 transition-all font-medium"
              >
                <LogOut className="mr-2 h-4 w-4" />
                Log Out
              </Button>
            </div>
          </div>
        </div>
      );
    }

    return children;
  };

  return (
    <>
      {showSplash && (
        <div
          className={`fixed inset-0 z-50 flex flex-col items-center justify-center bg-[#06030A] transition-opacity duration-700 ease-in-out ${
            fadeOut ? "opacity-0 pointer-events-none" : "opacity-100"
          }`}
        >
          <div className="flex flex-col items-center text-center">
            <div className="mb-6 shadow-[0_0_50px_rgba(142,121,244,0.25)] rounded-3xl">
              <ZoneLogo className="h-28 w-28" showRings={true} animate={true} />
            </div>
            <h1 className="text-5xl font-black tracking-tight text-white mb-2">
              Z.O.N.E.
            </h1>
            <p className="text-xs uppercase tracking-[0.25em] text-[#8E79F4] font-bold">
              by Eigenss
            </p>
          </div>
        </div>
      )}
      <div className={`transition-opacity duration-300 ${
        showSplash && !fadeOut ? "opacity-0" : "opacity-100"
      }`}>
        {renderContent()}
      </div>
    </>
  );
}
