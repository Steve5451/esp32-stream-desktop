'use strict'; // this server is a placeholder until it's rewritten in C# due to the lack of good screenshot/recording libraries

// fps will depend on your system. if your esp32 has large latency you'll have to lower screenCapInterval. doing so will limit your maximum fps.
// designed to have only 1 client

const net = require("net"),
	{ performance } = require("perf_hooks"),
	screenshot = require("screenshot-desktop"), // this screenshot library is horrible but it's all i could find. an executable is ran each time which takes about 100ms on my system. might benefit from SSD.
	sharp = require("sharp"),
	footer = Buffer.from([0x55, 0x44, 0x55, 0x11]), // both jpeg footer and incoming frame request from client
	port = 5451,
	resolution = { x: 240, y: 135 },
	jpegQuality = 50, // 0-100, higher is better. 50 gets 30-40fps. 70 will average 25-35.
	screenCapInterval = 25;
	  
var screenshotBuffer = { time: null, data: null },
	lastSentTime = null,
	frameRequestQueue = false,
	connection;

setInterval(captureScreenshot, screenCapInterval); // fetch many consecutive screenshots as a dirty workaround to the massive latency of the screenshot library.
  
async function captureScreenshot() {
	var startTime = performance.now();
	
	var screen = await screenshot({ format: "jpeg" });
	
	var width = resolution.x,
		height = Math.floor(width * 0.5625);

	var img = await sharp(screen);
	await img.resize(240, 135);
	await img.jpeg({
		quality: jpegQuality, 
		optimiseCoding: true,
		trellisQuantisation: true,
		overshootDeringing: false,
		chromaSubsampling: "4:2:0",
		progressive: false,
		mozjpeg: false,
		force: true
	});

	if(startTime > screenshotBuffer.time || !screenshotBuffer.data) {
		screenshotBuffer = { time: startTime, data: await img.toBuffer() };
		
		if(frameRequestQueue && connection) {
			frameRequestQueue = false;
			lastSentTime = startTime;
			
			getImage().then(buffer => connection.write(buffer, undefined, function() {
				//console.log("Frame sent")
			}));
		}
	} else {
		//console.log("Rejected old frame");
	}
}

const server = net.createServer(socket => {
	socket.setNoDelay(true);
	
	console.log("Client connected.");
	connection = socket;
	
	socket.on("data", data => {
		if(data.length === 4 && data[0] == footer[0] && data[1] == footer[1] && data[2] == footer[2] && data[3] == footer[3]) { // client is requesting new frame
			if(screenshotBuffer.time && screenshotBuffer.data && screenshotBuffer.time > lastSentTime) {
				lastSentTime = screenshotBuffer.time;
				
				getImage().then(buffer => socket.write(buffer, undefined, function() {
					//console.log("Frame sent")
				}));
			} else {
				frameRequestQueue = true;
				//console.log("Frame queued");
			}
		}
	});
	
	if(screenshotBuffer.data) {
		getImage().then(buffer => socket.write(buffer, undefined, function() {
			//console.log("Frame sent")
		}));
	} else {
		frameRequestQueue = true;
	}
	
	socket.on("end", () => {
		connection = null;
		console.log("Client disconnected")
	});
	
	socket.on("error", (err) => {
		if(err.code === "ECONNRESET") {
			//connection = null;
			console.log("Client connection reset");
		} else {
			console.error(err);
		}
	});
});

async function getImage() {
	var newBuffer = await Buffer.concat([screenshotBuffer.data, footer]); // append footer to signal end of data. 
	
	return newBuffer;
}

server.on("error", err => {
	throw err;
});

server.listen(port, () => {
  console.log("Server running");
});