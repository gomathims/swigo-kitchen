var CATALOG = {
  "voltage.fault": { channel: "fault", priority: "HIGH", apns: "10" },
  "current.fault": { channel: "fault", priority: "HIGH", apns: "10" },
  "device.offline": { channel: "availability", priority: "HIGH", apns: "10" },
  "schedule.failed": { channel: "schedule", priority: "HIGH", apns: "10" },
  "tank.full": { channel: "sensor", priority: "HIGH", apns: "10" },
  "flow.detected": { channel: "sensor", priority: "HIGH", apns: "10" },
  "device.on": { channel: "activity", priority: "NORMAL", apns: "5" },
  "device.off": { channel: "activity", priority: "NORMAL", apns: "5" },
  "device.online": { channel: "activity", priority: "NORMAL", apns: "5" }
};