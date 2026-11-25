#!/usr/bin/env python3
"""Capture the firmware UI with synthetic data. Requires Chrome and websocket-client."""
import base64
import json
import shutil
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path

import websocket

ROOT = Path(__file__).resolve().parents[1]


def main():
    chrome = shutil.which('google-chrome') or shutil.which('chromium')
    if not chrome:
        raise SystemExit('Google Chrome or Chromium required')
    preview = subprocess.Popen(['python3', str(ROOT / 'tools/preview.py')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    browser = None
    try:
        with tempfile.TemporaryDirectory(prefix='esp32-dmx-docs-') as profile:
            browser = subprocess.Popen([chrome, '--headless=new', '--no-sandbox',
                '--disable-gpu', '--no-proxy-server', '--remote-debugging-port=0',
                '--remote-allow-origins=http://localhost', '--user-data-dir=' + profile,
                'http://127.0.0.1:8765/'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            portfile = Path(profile) / 'DevToolsActivePort'
            for _ in range(100):
                if portfile.exists():
                    break
                time.sleep(.1)
            port = int(portfile.read_text().splitlines()[0])
            opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
            with opener.open(f'http://127.0.0.1:{port}/json/list') as r:
                page = next(v for v in json.load(r) if v['type'] == 'page')
            ws = websocket.create_connection(page['webSocketDebuggerUrl'], origin='http://localhost', timeout=15)
            ident = 0
            def call(method, params=None):
                nonlocal ident
                ident += 1
                ws.send(json.dumps(dict(id=ident, method=method, params=params or {})))
                while True:
                    response = json.loads(ws.recv())
                    if response.get('id') == ident:
                        if 'error' in response:
                            raise RuntimeError(response['error'])
                        return response.get('result', {})
            def evaluate(expression):
                result = call('Runtime.evaluate', dict(expression=expression, returnByValue=True))
                if result.get('exceptionDetails'):
                    raise RuntimeError(result['exceptionDetails'])
                return result.get('result', {}).get('value')
            call('Page.enable')
            for _ in range(100):
                if evaluate("document.readyState==='complete' && typeof fixtureProfiles!=='undefined'"):
                    break
                time.sleep(.1)
            time.sleep(.5)
            captures = [('live-desktop','live',1440,1050,False),
                        ('laser-desktop','live',1440,1100,True),
                        ('programs-desktop','program',1440,1050,False),
                        ('config-desktop','config',1440,1100,False),
                        ('live-mobile','live',390,844,False)]
            for name, tab, width, height, laser in captures:
                call('Emulation.setDeviceMetricsOverride', dict(width=width,height=height,deviceScaleFactor=1,mobile=width<600))
                evaluate("document.getElementById('liveProfile').value='she-fs403a-7ch';syncProfileSelects('live');updateProfileUi();")
                if laser:
                    evaluate("document.getElementById('liveProfile').value='laser10';syncProfileSelects('live');updateProfileUi();")
                if tab == 'program':
                    evaluate("loadDemoProgram('ocean')")
                evaluate(f"setTab('{tab}',document.querySelector(\"button[onclick*=\\\"setTab('{tab}'\\\"]\"));window.scrollTo(0,0)")
                time.sleep(.25)
                overflow = evaluate('document.documentElement.scrollWidth > innerWidth')
                if overflow:
                    raise RuntimeError('horizontal overflow: ' + name)
                options = dict(format='png', captureBeyondViewport=False)
                if name in ('laser-desktop', 'config-desktop'):
                    layout = call('Page.getLayoutMetrics')['cssContentSize']
                    options.update(captureBeyondViewport=True, clip=dict(
                        x=0, y=0, width=width, height=layout['height'], scale=1))
                image = call('Page.captureScreenshot',options)
                (ROOT / 'docs/images' / (name+'.png')).write_bytes(base64.b64decode(image['data']))
                print('Captured',name,flush=True)
            ws.close()
            browser.terminate()
            browser.wait(timeout=10)
            browser = None
    finally:
        if browser:
            browser.terminate()
            browser.wait(timeout=10)
        preview.terminate()
        preview.wait(timeout=10)


if __name__ == '__main__':
    main()
