let callbackCounter = 0;
function getUniqueCallbackName(prefix) {
  return `${prefix}_callback_${Date.now()}_${callbackCounter++}`;
}

export function exec(command, options) {
  if (typeof options === "undefined") {
    options = {};
  }

  return new Promise((resolve, reject) => {
    // Generate a unique callback function name
    const callbackFuncName = getUniqueCallbackName("exec");

    // Define the success callback function
    window[callbackFuncName] = (errno, stdout, stderr) => {
      resolve({ errno, stdout, stderr });
      cleanup(callbackFuncName);
    };

    function cleanup(successName) {
      delete window[successName];
    }

    try {
      ksu.exec(command, JSON.stringify(options), callbackFuncName);
    } catch (error) {
      reject(error);
      cleanup(callbackFuncName);
    }
  });
}

function Stdio() {
  this.listeners = {};
}

Stdio.prototype.on = function (event, listener) {
  if (!this.listeners[event]) {
    this.listeners[event] = [];
  }
  this.listeners[event].push(listener);
};

Stdio.prototype.emit = function (event, ...args) {
  if (this.listeners[event]) {
    this.listeners[event].forEach((listener) => listener(...args));
  }
};

function ChildProcess() {
  this.listeners = {};
  this.stdin = new Stdio();
  this.stdout = new Stdio();
  this.stderr = new Stdio();
}

ChildProcess.prototype.on = function (event, listener) {
  if (!this.listeners[event]) {
    this.listeners[event] = [];
  }
  this.listeners[event].push(listener);
};

ChildProcess.prototype.emit = function (event, ...args) {
  if (this.listeners[event]) {
    this.listeners[event].forEach((listener) => listener(...args));
  }
};

export function spawn(command, args, options) {
  if (typeof args === "undefined") {
    args = [];
  } else if (!(args instanceof Array)) {
    // allow for (command, options) signature
    options = args;
  }

  if (typeof options === "undefined") {
    options = {};
  }

  const child = new ChildProcess();
  const childCallbackName = getUniqueCallbackName("spawn");
  window[childCallbackName] = child;

  function cleanup(name) {
    delete window[name];
  }

  child.on("exit", (code) => {
    cleanup(childCallbackName);
  });

  try {
    ksu.spawn(
      command,
      JSON.stringify(args),
      JSON.stringify(options),
      childCallbackName,
    );
  } catch (error) {
    child.emit("error", error);
    cleanup(childCallbackName);
  }
  return child;
}

export function fullScreen(isFullScreen) {
  ksu.fullScreen(isFullScreen);
}

export function toast(message) {
  ksu.toast(message);
}

export function moduleInfo() {
  return ksu.moduleInfo();
}

export async function listPackages(type = "all") {
  if (typeof globalThis.ksu?.listPackages === "function") {
    try {
      return JSON.parse(ksu.listPackages(type));
    } catch (error) {
      // Fallback
    }
  }

  return new Promise((resolve, reject) => {
    const pmArgs = {
      all: [],
      user: ["-3"],
      system: ["-s"],
    };

    const pkgs = [];
    const cp = spawn("pm", ["list", "packages", ...(pmArgs[type] || [])]);
    
    cp.stdout.on("data", (data) => {
      data.split("\n").forEach(line => {
        if (line.trim()) {
          pkgs.push(line.trim().replace(/^package:/, ""));
        }
      });
    });
    
    cp.on("exit", (code) => {
      if (code === 0) resolve(pkgs);
      else reject(new Error("pm list packages failed"));
    });
  });
}

export function getPackagesInfo(pkg) {
  return new Promise((resolve, reject) => {
    if (!pkg) return resolve([]);
    if (typeof globalThis.ksu?.getPackagesInfo !== "function") {
      return resolve(Array.isArray(pkg) ? pkg.map(p => ({ packageName: p, appLabel: p })) : { packageName: pkg, appLabel: pkg });
    }

    const pkgs = Array.isArray(pkg) ? pkg : [pkg];
    try {
      const infoJson = ksu.getPackagesInfo(JSON.stringify(pkgs));
      const result = JSON.parse(infoJson);
      resolve(Array.isArray(pkg) ? result : result[0]);
    } catch (error) {
      reject(error);
    }
  });
}
