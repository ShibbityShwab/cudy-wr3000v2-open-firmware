const net = require('net');
const http = require('http');
const { URL } = require('url');
const PORT = parseInt(process.argv[2] || '8899', 10);

const server = http.createServer((req, res) => {
  let target;
  try {
    target = new URL(req.url);
  } catch {
    res.writeHead(400);
    res.end('absolute URL required');
    return;
  }
  const opts = {
    host: target.hostname,
    port: target.port || 80,
    path: target.pathname + target.search,
    method: req.method,
    headers: { ...req.headers, host: target.host },
  };
  delete opts.headers.connection;
  const upstream = http.request(opts, (r) => {
    res.writeHead(r.statusCode, r.headers);
    r.pipe(res);
  });
  upstream.on('error', () => {
    res.writeHead(502);
    res.end('upstream error');
  });
  req.pipe(upstream);
});

server.on('connect', (req, clientSocket, head) => {
  const [host, port] = req.url.split(':');
  const upstream = net.connect(parseInt(port || '443', 10), host, () => {
    clientSocket.write('HTTP/1.1 200 Connection Established\r\n\r\n');
    upstream.write(head);
    upstream.pipe(clientSocket);
    clientSocket.pipe(upstream);
  });
  upstream.on('error', () => clientSocket.destroy());
  clientSocket.on('error', () => upstream.destroy());
});

server.listen(PORT, '127.0.0.1', () => console.log('proxy listening on 127.0.0.1:' + PORT));
