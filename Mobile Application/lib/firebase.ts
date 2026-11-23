import { initializeApp, getApps, getApp } from "firebase/app";
import { getAuth } from "firebase/auth";
import { getDatabase } from "firebase/database";

const firebaseConfig = {
  apiKey: "AIzaSyBSDsX8rkYPWMBzlHU2tioMeiJ28W4MESs",
  authDomain: "zone-c0177.firebaseapp.com",
  databaseURL: "https://zone-c0177-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "zone-c0177",
  storageBucket: "zone-c0177.firebasestorage.app",
  messagingSenderId: "443557593162",
  appId: "1:443557593162:web:553a5a68921a048d2ce6b3"
};

// Initialize Firebase only if it hasn't been initialized already
const app = !getApps().length ? initializeApp(firebaseConfig) : getApp();
const auth = getAuth(app);
const database = getDatabase(app);

export { app, auth, database };
