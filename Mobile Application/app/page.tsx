"use client";

import { useState, useEffect } from "react";
import { useRouter } from "next/navigation";
import { signInWithEmailAndPassword, createUserWithEmailAndPassword, sendEmailVerification, onAuthStateChanged } from "firebase/auth";
import { auth } from "@/lib/firebase";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Loader2, Lock, Mail, ShieldAlert } from "lucide-react";
import { ZoneLogo } from "@/components/ui/zone-logo";
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
  DialogTrigger,
} from "@/components/ui/dialog";

export default function LoginPage() {
  const router = useRouter();
  const [email, setEmail] = useState("");
  const [password, setPassword] = useState("");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  const [isRegOpen, setIsRegOpen] = useState(false);
  const [regEmail, setRegEmail] = useState("");
  const [regPassword, setRegPassword] = useState("");
  const [regConfirmPassword, setRegConfirmPassword] = useState("");
  const [regLoading, setRegLoading] = useState(false);
  const [regError, setRegError] = useState("");

  const [showSplash, setShowSplash] = useState(true);
  const [fadeOut, setFadeOut] = useState(false);

  useEffect(() => {
    let outerTimer: NodeJS.Timeout;
    let innerTimer: NodeJS.Timeout;

    if (typeof window !== "undefined" && sessionStorage.getItem("zone_splash_shown") === "true") {
      setShowSplash(false);
    }

    const unsubscribe = onAuthStateChanged(auth, (user) => {
      if (user) {
        router.push("/dashboard");
      }
      
      const hasShown = typeof window !== "undefined" && sessionStorage.getItem("zone_splash_shown") === "true";
      if (!hasShown) {
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
    });

    return () => {
      unsubscribe();
      clearTimeout(outerTimer);
      clearTimeout(innerTimer);
    };
  }, [router]);
  const handleLogin = async (e: React.FormEvent) => {
    e.preventDefault();

    if (typeof window !== "undefined" && !window.navigator.onLine) {
      setError("No internet connection detected. Please check your connection and try again.");
      return;
    }

    setLoading(true);
    setError("");

    try {
      await signInWithEmailAndPassword(auth, email, password);
      router.push("/dashboard");
    } catch (err: any) {
      console.error("Login error:", err);
      if (err.code === "auth/network-request-failed" || err.message?.includes("network")) {
        setError("No internet connection detected. Please check your connection and try again.");
      } else {
        setError("Invalid email or password. Please try again.");
      }
    } finally {
      setLoading(false);
    }
  };

  const handleRegister = async (e: React.FormEvent) => {
    e.preventDefault();
    setRegError("");

    if (typeof window !== "undefined" && !window.navigator.onLine) {
      setRegError("No internet connection detected. Please check your connection and try again.");
      return;
    }

    if (regPassword.length < 6) {
      setRegError("Password must be at least 6 characters long.");
      return;
    }

    if (regPassword !== regConfirmPassword) {
      setRegError("Passwords do not match.");
      return;
    }

    setRegLoading(true);
    try {
      const userCredential = await createUserWithEmailAndPassword(auth, regEmail, regPassword);
      await sendEmailVerification(userCredential.user);
      router.push("/dashboard");
    } catch (err: any) {
      console.error("Register error:", err);
      if (err.code === "auth/network-request-failed" || err.message?.includes("network")) {
        setRegError("No internet connection detected. Please check your connection and try again.");
      } else if (err.code === "auth/email-already-in-use") {
        setRegError("An account already exists for this email address.");
      } else if (err.code === "auth/weak-password") {
        setRegError("Password should be at least 6 characters.");
      } else {
        setRegError("Registration failed. Please try again.");
      }
    } finally {
      setRegLoading(false);
    }
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
      <div className={`flex min-h-screen items-center justify-center bg-[#06030A] relative overflow-hidden p-4 transition-opacity duration-300 ${
        showSplash && !fadeOut ? "opacity-0" : "opacity-100"
      }`}>
        {/* BACKGROUND GLOWING ORBS */}
        <div className="ambient-blob-1" />
        <div className="ambient-blob-2" />
        <div className="ambient-blob-3" />
        
        <div className="relative z-10 w-full max-w-md">
          {/* Logo/Icon Area */}
          <div className="mb-8 flex flex-col items-center">
            <div className="mb-4 shadow-[0_0_30px_rgba(142,121,244,0.2)] rounded-2xl">
              <ZoneLogo className="h-16 w-16" showRings={true} animate={false} />
            </div>
            <h1 className="text-3xl font-bold tracking-tight text-white">Z.O.N.E. Admin</h1>
            <p className="mt-2 text-sm text-white/50">Secure Dashboard Authentication</p>
          </div>

          {/* Login Card */}
          <div className="overflow-hidden rounded-[26px] border border-white/8 bg-white/4 backdrop-blur-2xl shadow-2xl w-full glass-card p-8">
            <div className="border-b border-white/5 pb-4 mb-6">
              <h2 className="text-lg font-bold text-white">Log In</h2>
              <p className="text-xs text-white/50">Enter your credentials to continue.</p>
            </div>
            
            <form onSubmit={handleLogin} className="space-y-6">
              {error && (
                <div className="flex items-center gap-2 rounded-lg bg-destructive/10 p-4 text-sm text-destructive border border-destructive/20">
                  <ShieldAlert className="h-4 w-4 shrink-0" />
                  <p>{error}</p>
                </div>
              )}

              <div className="space-y-4">
                <div className="space-y-2">
                  <label className="text-xs font-bold leading-none text-white/60 uppercase tracking-wide" htmlFor="email">
                    Email
                  </label>
                  <div className="relative">
                    <Mail className="absolute left-3 top-3 h-4 w-4 text-white/40" />
                    <Input 
                      id="email" 
                      type="email" 
                      className="pl-9 bg-black/40 border-white/10 text-white focus-visible:ring-white/20 h-10 rounded-xl"
                      value={email}
                      onChange={(e) => setEmail(e.target.value)}
                      required
                    />
                  </div>
                </div>
                
                <div className="space-y-2">
                  <div className="flex items-center justify-between">
                    <label className="text-xs font-bold leading-none text-white/60 uppercase tracking-wide" htmlFor="password">
                      Password
                    </label>
                  </div>
                  <div className="relative">
                    <Lock className="absolute left-3 top-3 h-4 w-4 text-white/40" />
                    <Input 
                      id="password" 
                      type="password" 
                      className="pl-9 bg-black/40 border-white/10 text-white focus-visible:ring-white/20 h-10 rounded-xl"
                      value={password}
                      onChange={(e) => setPassword(e.target.value)}
                      required
                    />
                  </div>
                </div>
              </div>

              <Button 
                type="submit" 
                className="w-full clay-gradient-btn h-10 rounded-xl font-semibold mt-2"
                disabled={loading}
              >
                {loading ? (
                  <>
                    <Loader2 className="mr-2 h-4 w-4 animate-spin" />
                    Authenticating...
                  </>
                ) : (
                  "Log In"
                )}
              </Button>
              
              <div className="mt-4 text-center text-sm text-white/50">
                Not Registered?{" "}
                <Dialog open={isRegOpen} onOpenChange={setIsRegOpen}>
                  <DialogTrigger asChild>
                    <button type="button" className="text-[#8E79F4] hover:underline font-semibold cursor-pointer">
                      Sign up
                    </button>
                  </DialogTrigger>
                  <DialogContent className="sm:max-w-[425px] border-white/10 bg-[#161224]/95 text-white backdrop-blur-2xl">
                    <DialogHeader>
                      <DialogTitle className="text-white border-b border-white/5 pb-4">Create an Account</DialogTitle>
                      <DialogDescription className="pt-2 text-white/50">
                        Register with your email to access the Z.O.N.E. Admin Console.
                      </DialogDescription>
                    </DialogHeader>
                    
                    <form onSubmit={handleRegister} className="space-y-4 py-2">
                      {regError && (
                        <div className="flex items-center gap-2 rounded-lg bg-destructive/10 p-3 text-sm text-destructive border border-destructive/20">
                          <ShieldAlert className="h-4 w-4 shrink-0" />
                          <p>{regError}</p>
                        </div>
                      )}
                      
                      <div className="space-y-2">
                        <label className="text-sm font-medium text-white" htmlFor="reg-email">
                          Email
                        </label>
                        <Input 
                          id="reg-email" 
                          type="email" 
                          className="bg-black/40 border-white/10 text-white focus-visible:ring-white/20 h-10 rounded-xl"
                          value={regEmail}
                          onChange={(e) => setRegEmail(e.target.value)}
                          required
                        />
                      </div>
                      
                      <div className="space-y-2">
                        <label className="text-sm font-medium text-white" htmlFor="reg-password">
                          Password
                        </label>
                        <Input 
                          id="reg-password" 
                          type="password" 
                          className="bg-black/40 border-white/10 text-white focus-visible:ring-white/20 h-10 rounded-xl"
                          value={regPassword}
                          onChange={(e) => setRegPassword(e.target.value)}
                          required
                        />
                      </div>
                      
                      <div className="space-y-2">
                        <label className="text-sm font-medium text-white" htmlFor="reg-confirm">
                          Re-enter Password
                        </label>
                        <Input 
                          id="reg-confirm" 
                          type="password" 
                          className="bg-black/40 border-white/10 text-white focus-visible:ring-white/20 h-10 rounded-xl"
                          value={regConfirmPassword}
                          onChange={(e) => setRegConfirmPassword(e.target.value)}
                          required
                        />
                      </div>
                      
                      <DialogFooter className="pt-4 border-t border-white/5">
                        <Button 
                          type="button" 
                          variant="outline" 
                          onClick={() => setIsRegOpen(false)}
                          className="text-white border-white/10 hover:bg-white/5"
                        >
                          Cancel
                        </Button>
                        <Button 
                          type="submit" 
                          disabled={regLoading}
                          className="clay-gradient-btn"
                        >
                          {regLoading ? <Loader2 className="h-4 w-4 animate-spin mr-2" /> : null}
                          Register
                        </Button>
                      </DialogFooter>
                    </form>
                  </DialogContent>
                </Dialog>
              </div>
            </form>
          </div>
          
          <p className="mt-8 text-center text-xs text-white/30">
            &copy; {new Date().getFullYear()} Z.O.N.E. Security Systems. All rights reserved.
          </p>
        </div>
      </div>
    </>
  );
}
