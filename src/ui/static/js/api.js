// api.js - talks to src/ui/server.py, which forwards every command to the
// C program (bin/banker -j). Each response is the JSON line printed by C.

async function call(method, url, body) {
  const options = { method, headers: {} };
  if (body !== undefined) {
    options.headers["Content-Type"] = "application/json";
    options.body = JSON.stringify(body);
  }
  let response;
  try {
    response = await fetch(url, options);
  } catch (err) {
    throw new Error("Сервертэй холбогдож чадсангүй. server.py ажиллаж байгаа эсэхийг шалгана уу.");
  }
  const data = await response.json();
  if (!response.ok) {
    throw new Error(data.error || `HTTP ${response.status}`);
  }
  return data;
}

export const api = {
  state: () => call("GET", "/api/state"),
  examples: () => call("GET", "/api/examples"),
  command: (command) => call("POST", "/api/command", { command }),
  // stops at the first command the C program reports as an error
  batch: (commands) => call("POST", "/api/batch", { commands, stop_on_error: true }),
  restart: () => call("POST", "/api/restart"),
};
