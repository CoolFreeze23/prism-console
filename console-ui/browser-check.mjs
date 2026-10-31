// Headless renderer regression check. Fixture data, not a native integration test.
import { chromium } from 'playwright-core'
import { createServer } from 'node:http'
import { readFile, mkdir } from 'node:fs/promises'
import { resolve, extname, sep } from 'node:path'
import assert from 'node:assert/strict'
const root = resolve('dist')
const server = createServer(async (req, res) => {
  const file = resolve(root, '.' + new URL(req.url, 'http://localhost').pathname.replace(/\/$/, '/index.html'))
  if (!file.startsWith(root + sep)) { res.writeHead(403).end(); return }
  try { res.setHeader('Content-Type', ({ '.html':'text/html', '.js':'text/javascript', '.css':'text/css' })[extname(file)] || 'application/octet-stream'); res.end(await readFile(file)) }
  catch { res.writeHead(404).end() }
})
await new Promise(r => server.listen(0, '127.0.0.1', r))
const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe', headless: true })
try {
  const page = await browser.newPage()
  const errors = []
  page.on('pageerror', e => errors.push(String(e)))
  await page.addInitScript(() => {
    window.testCalls = []
    window.qt = { webChannelTransport: {} }
    window.QWebChannel = class {
      constructor(_, callback) {
        callback({ objects: { prism: {
          changed: { connect(callback) { window.notifyFixture = callback } },
          request(method, args, done) {
            window.testCalls.push([method, args])
            done({ ok: true, data: method === 'snapshot' ? {
              instances: [{ id: 'fixture', name: 'CrazyCraft 5.0', group: 'Test library', version: '1.21.1', icon: 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><rect width="100" height="100" rx="15" fill="%237a8cff"/></svg>', running: false, canLaunch: true, minMemory: 2048, maxMemory: 10240 }],
              accounts: [{ id: 'test', name: 'Test player', selected: true }], versions: ['1.21.1'],
            } : method === 'workflow' ? window.workflowFixture || null : method === 'resources' ? [{ id: 'mod-id', name: 'Example mod', enabled: true, version: '1.0' }] : null })
          }
        } } })
      }
    }
  })
  await mkdir('artifacts', { recursive: true })
  for (const [width, height] of [[1280,800], [800,600]]) {
    await page.setViewportSize({ width, height })
    await page.goto(`http://127.0.0.1:${server.address().port}`)
    await page.getByRole('heading', { name: 'CrazyCraft 5.0' }).waitFor()
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false)
    await page.getByRole('button', { name: 'Configure', exact: true }).click()
    await page.getByRole('dialog').waitFor()
    await page.keyboard.press('Escape')
    assert.equal(await page.getByRole('dialog').count(), 0)
    assert.equal(await page.getByRole('button', { name: 'Configure', exact: true }).evaluate(e => e === document.activeElement), true)
    await page.getByRole('button', { name: 'Mods & packs', exact: true }).click()
    await page.getByRole('switch', { name: /Example mod/ }).click()
    const calls = await page.evaluate(() => window.testCalls.filter(c => c[0] === 'setResourceEnabled'))
    assert.equal(calls.length, 1)
    assert.equal(calls[0][1].resourceId, 'mod-id')
    assert.equal(calls[0][1].enabled, false)
    await page.keyboard.press('Escape')
    await page.screenshot({ path: `artifacts/library-fixture-${width}.png` })
  }
  // Controller already present at page load: no initial connection event.
  await page.addInitScript(() => {
    window.padFixture = { id: 'Microsoft X-Box 360 pad 0', index: 0, connected: true, mapping: 'standard', timestamp: 0,
      axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })) }
    Object.defineProperty(navigator, 'getGamepads', { value: () => window.padFixture.connected ? [window.padFixture] : [] })
    window.GamepadEvent = class extends Event { constructor(type, options) { super(type); this.gamepad = options.gamepad } }
  })
  await page.setViewportSize({ width: 1280, height: 800 })
  await page.goto(`http://127.0.0.1:${server.address().port}`)
  await page.getByRole('button', { name: '▶ Play', exact: true }).waitFor()
  await page.waitForTimeout(700)
  const prior = await page.evaluate(() => document.activeElement.textContent)
  await page.evaluate(() => { window.padFixture.axes[0] = 1 })
  await page.waitForFunction(p => document.activeElement.textContent !== p, prior)
  await page.evaluate(() => { window.padFixture.axes[0] = 0; window.padFixture.connected = false })
  await page.waitForTimeout(650)
  const disconnectedFocus = await page.evaluate(() => document.activeElement.textContent)
  await page.evaluate(() => { window.padFixture.connected = true; window.padFixture.axes[1] = 1 })
  await page.waitForFunction(p => document.activeElement.textContent !== p, disconnectedFocus)
  await page.evaluate(() => { window.padFixture.axes[1] = 0 })
  await page.evaluate(() => {
    const canvas = document.createElement('canvas'); canvas.width = canvas.height = 64
    const ctx = canvas.getContext('2d')
    ctx.fillStyle = '#ad744d'; ctx.fillRect(0,0,64,32)
    ctx.fillStyle = '#31aaaa'; ctx.fillRect(16,16,24,16)
    ctx.fillStyle = '#414192'; ctx.fillRect(0,16,16,16); ctx.fillRect(16,48,16,16)
    ctx.fillStyle = '#ad744d'; ctx.fillRect(32,48,16,16)
    ctx.fillStyle = '#ffffff'; ctx.fillRect(10,12,2,1); ctx.fillRect(14,12,2,1)
    window.workflowFixture = { id: 'skin-test', title: 'Skin preview', content: { id: 'skin', type: 'group', skin: canvas.toDataURL(), children: [] } }
    window.notifyFixture()
  })
  await page.getByRole('img', { name: 'Minecraft skin preview' }).waitFor()
  await page.getByRole('button', { name: 'Rotate right', exact: true }).click()
  await page.screenshot({ path: 'artifacts/skin-preview.png' })
  assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false)
  assert.deepEqual(errors, [])
  console.log('Controller regression passed: already-connected pad, stick navigation, disconnect and reconnect.')
  console.log('Renderer smoke passed: 1280×800 and 800×600; modal focus restoration; one explicit resource mutation; no page errors. Fixture data only.')
} finally { await browser.close(); server.close() }
