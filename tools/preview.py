#!/usr/bin/env python3
"""Render the real firmware UI with local mock APIs; never sends DMX."""
import json
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def firmware_html():
    source = (ROOT / 'src/main.cpp').read_text()
    start = source.index('static const char INDEX_HTML[]')
    end = source.index('static void handleIndex()', start)
    source = source[start:end]
    parts = re.findall(r'R"HTML\((.*?)\)HTML"|#include "profiles_ui.h"', source, re.S)
    if len(parts) != 3:
        raise ValueError('unexpected firmware HTML layout')
    extension = (ROOT / 'src/profiles_ui.h').read_text()
    extension = re.fullmatch(r'\s*R"JS\((.*)\)JS"\s*', extension, re.S).group(1)
    return (parts[0] + extension + parts[2]).encode()


CONFIG = dict(hostname='esp32-dmx', uniqueNames=True, apEnabled=True,
              apSsid='DMX-Node-C3', apPass='dmx12345', apIp='192.168.44.1',
              staEnabled=False, staSsid='', staPass='', ethEnabled=True,
              ethDhcp=True, ethIp='192.168.1.50', ethGateway='192.168.1.1',
              ethSubnet='255.255.255.0', ethDns='192.168.1.1', universe=0)
STATUS = dict(fw='1.1.0', hw='esp32-eth01', effectiveHostname='esp32-dmx-DEMO',
              macSuffix='DEMO', uniqueNames=True, wifiMode='AP', wifiRuntime=True,
              wifiSuppressedByEth=False, apEnabled=True, apIp='192.168.44.1',
              staConnected=False, staIp='', ethEnabled=True, ethDhcp=True,
              ethLink=False, ethIp='', universe=0, framesSent=12540,
              artnetPackets=250, lastArtnetAgeMs=1200, activeDemo='off', freeHeap=180000)


class Handler(BaseHTTPRequestHandler):
    profiles = {}

    def reply(self, data, content_type='application/json', status=200):
        if not isinstance(data, bytes):
            data = json.dumps(data).encode() if content_type == 'application/json' else data.encode()
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == '/api/status':
            return self.reply(STATUS)
        if self.path == '/api/config':
            return self.reply(CONFIG)
        if self.path == '/api/profiles':
            return self.reply(self.profiles)
        if self.path == '/api/logs':
            return self.reply('[0s] Offline documentation preview\n[1s] Synthetic status: no DMX output\n', 'text/plain')
        if self.path.startswith('/api/'):
            return self.reply({'error': 'mock endpoint unavailable'}, status=404)
        return self.reply(firmware_html(), 'text/html; charset=utf-8')

    def do_POST(self):
        data = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        if self.path == '/api/profiles':
            try:
                value = json.loads(data)
                if not isinstance(value, dict):
                    raise ValueError('object required')
                type(self).profiles = value
            except ValueError:
                return self.reply('Invalid JSON', 'text/plain', 400)
        return self.reply('OK (offline preview, no hardware output)', 'text/plain')


if __name__ == '__main__':
    print('Offline firmware UI: http://127.0.0.1:8765/', flush=True)
    ThreadingHTTPServer(('127.0.0.1', 8765), Handler).serve_forever()
