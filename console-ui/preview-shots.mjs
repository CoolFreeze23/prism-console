// Renders the built UI against fixture data and saves screenshots to artifacts/preview-*.png.
// Fixture only: no Prism process is involved. Usage: node preview-shots.mjs (after pnpm build).
import { chromium } from 'playwright-core'
import { createServer } from 'node:http'
import { readFile, mkdir } from 'node:fs/promises'
import { resolve, extname, sep } from 'node:path'
const root = resolve('dist')
const types = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.svg': 'image/svg+xml', '.woff2': 'font/woff2' }
const server = createServer(async (req, res) => {
  const file = resolve(root, '.' + new URL(req.url, 'http://localhost').pathname.replace(/\/$/, '/index.html'))
  if (!file.startsWith(root + sep)) { res.writeHead(403).end(); return }
  try { res.setHeader('Content-Type', types[extname(file)] || 'application/octet-stream'); res.end(await readFile(file)) } catch { res.writeHead(404).end() }
})
await new Promise(r => server.listen(0, '127.0.0.1', r))

// Approximate the adapter's output for the captured launcher settings page: radio groups, switches, captions.
function adapt(node) {
  if (!node || typeof node !== 'object') return node
  const out = { ...node }
  if (out.type === 'button' && out.checked !== undefined && out.checked !== null) {
    const radio = /^(sortBy|sortLast|askToRename|alwaysRename|neverRename)/.test(out.label || '')
    out.role = radio ? 'radio' : 'check'
    if (radio) out.group = /^sort/.test(out.label) ? 'sorting' : 'renaming'
    else if (out.label && out.label !== out.text) out.hint = out.label
  }
  if (typeof out.label === 'string') out.label = out.label.replace(/:\s*$/, '')
  if (out.type === 'label' && typeof out.text === 'string' && out.text.length < 60) out.text = out.text.replace(/:\s*$/, '')
  if (Array.isArray(out.children)) {
    const fields = new Set(out.children.filter(c => ['input', 'number', 'select'].includes(c.type)).map(c => (c.label || '').replace(/:\s*$/, '')))
    out.children = out.children.map(adapt).filter(c => !(c.type === 'label' && fields.has(c.text)))
    const box = out.children.findIndex(c => c.children?.length === 3 && c.children.every(b => ['OK', 'Cancel', 'Help'].includes(b.text)))
    if (box >= 0) { const [buttons] = out.children.splice(box, 1); out.children.push({ ...buttons, type: 'buttonbox' }) }
  }
  return out
}
const captured = JSON.parse(await readFile('artifacts/workflow-launcher.json', 'utf8'))
const settings = { ...captured, content: adapt(captured.content) }

const lang = process.env.PREVIEW_LANG || 'en_US'
const suffix = lang === 'en_US' ? '' : '-' + lang
const overflows = []
const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe', headless: true })
try {
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } })
  const errors = []
  page.on('pageerror', e => errors.push(String(e)))
  await page.addInitScript(settingsFixture => {
    const icon = 'data:image/svg+xml,' + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" shape-rendering="crispEdges"><rect width="16" height="16" fill="#6b4a2b"/><rect width="16" height="5" fill="#4f9a2f"/><rect y="5" width="16" height="2" fill="#3d7a22"/><rect x="3" y="9" width="2" height="2" fill="#7d5a37"/><rect x="10" y="11" width="2" height="2" fill="#56391f"/></svg>')
    window.fixture = {
      launch: null, workflow: null,
      instances: [
        { id: 'cc5', name: 'CrazyCraft 5.0', group: 'Modpacks', version: '1.21.1', icon, running: false, launching: false, canLaunch: true, minMemory: 4096, maxMemory: 10240 },
        { id: 'vanilla', name: 'Vanilla 1.21.1', group: '', version: '1.21.1', icon: '', running: false, launching: false, canLaunch: true, minMemory: 2048, maxMemory: 4096 },
      ],
    }
    window.makeSkin = () => {
      const c = document.createElement('canvas'); c.width = c.height = 64; const x = c.getContext('2d')
      x.fillStyle = '#c69c6d'; x.fillRect(8, 8, 8, 8); x.fillStyle = '#4a2f1b'; x.fillRect(8, 8, 8, 3)
      x.fillStyle = '#ffffff'; x.fillRect(9, 12, 2, 1); x.fillRect(13, 12, 2, 1); x.fillStyle = '#3a5fcd'; x.fillRect(10, 12, 1, 1); x.fillRect(14, 12, 1, 1)
      x.fillStyle = '#8a5a44'; x.fillRect(11, 14, 2, 1)
      return c.toDataURL()
    }
    window.qt = { webChannelTransport: {} }
    window.QWebChannel = class {
      constructor(_, callback) {
        callback({ objects: { prism: {
          changed: { connect(cb) { window.notify = cb } },
          request(method, args, done) {
            const f = window.fixture
            const data = method === 'snapshot' ? {
              instances: f.instances.map(i => i.id === 'cc5' && f.launch?.active ? { ...i, launching: true, canLaunch: false } : i),
              accounts: [{ id: 'player', name: 'CoolFreeze23', selected: true }], language: window.previewLang,
              modpacksRevision: JSON.stringify(f.modpacks || {}).length,
              launches: f.launch ? { cc5: f.launch } : {},
            } : method === 'modpacks' ? f.modpacks || {} : method === 'modpackIcons' ? Object.fromEntries((args.ids || []).filter(id => f.modpackIcons?.[id]).map(id => [id, f.modpackIcons[id]])) : method === 'workflow' ? f.workflow : method === 'launcherOptions' ? { soundEnabled: true, soundVolume: 35, installDependencies: true, trackMetadata: true, askModpackUpdate: true, downloadGameFiles: true, parallelDownloads: 6, retryLimit: 3, timeoutSeconds: 60, recordPlayTime: true, showPlayTime: true, lowMemoryWarning: true, about: { name: 'Prism Launcher', version: '10.0.0-console', commit: '8708e1c0a1', qt: '6.10.1' } }
              : method === 'worlds' ? [] : method === 'servers' ? [{ name: 'Hypixel', address: 'mc.hypixel.net', icon: '' }, { name: 'Home server', address: '192.168.8.20:25565', icon: '' }] : method === 'accountDetails' ? { name: 'CoolFreeze23', skin: window.makeSkin(), capes: [] } : null
            done({ ok: true, data })
          },
        } } })
      }
    }
    window.settingsFixture = settingsFixture
  }, settings)
  await page.addInitScript(language => { window.previewLang = language }, lang)
  // Every screenshot also records text that does not fit its box (buttons, chips, headings, labels, hints).
  const shoot = async name => {
    await page.screenshot({ path: `artifacts/preview-${name}${suffix}.png` })
    const found = await page.evaluate(() => [...document.querySelectorAll('button, .bp-chip, h1, h2, h3, .setting-text > span, .hint, .launch-steps li, .eyebrow')]
      .filter(e => e.offsetParent && (e.scrollWidth > e.clientWidth + 2 || (getComputedStyle(e).whiteSpace === 'nowrap' && e.scrollWidth > e.clientWidth + 2)))
      .map(e => e.textContent.trim().slice(0, 60)))
    for (const text of found) overflows.push(`${name}: ${text}`)
  }
  await mkdir('artifacts', { recursive: true })
  await page.goto(`http://127.0.0.1:${server.address().port}`)
  await page.getByRole('heading', { name: 'CrazyCraft 5.0' }).waitFor()
  await page.waitForTimeout(400)
  await shoot('library')

  await page.evaluate(() => { window.fixture.launch = { active: true, stage: 'files', status: 'Downloading assets (1,204 / 3,390)', current: 1204, total: 3390, startedAt: Date.now() - 14000, stageAt: Date.now() - 9000, changedAt: Date.now() }; window.notify() })
  await page.locator('.launch-overlay').waitFor()
  await page.waitForTimeout(400)
  await shoot('launch')

  await page.evaluate(() => { window.fixture.launch = { active: false, failed: true, status: 'Minecraft account refresh timed out. Check your connection, then try again.' }; window.notify() })
  await page.locator('[data-action=launch-retry]').waitFor()
  await shoot('launch-failed')
  await page.locator('[data-action=launch-close]').click()

  await page.evaluate(() => { window.fixture.workflow = window.settingsFixture; window.notify() })
  await page.locator('.native-workflow').waitFor()
  await page.waitForTimeout(300)
  await shoot('settings')
  await page.locator('.dialog-body').evaluate(e => { e.scrollTop = 900 })
  await shoot('settings-scrolled')

  await page.evaluate(() => { window.fixture.workflow = null; window.notify() })
  await page.locator('.native-workflow').waitFor({ state: 'detached' })
  await page.locator('[data-action=play]').click()
  await page.setViewportSize({ width: 800, height: 600 })
  await page.waitForTimeout(300)
  await shoot('library-800')
  await page.setViewportSize({ width: 1280, height: 800 })
  await page.evaluate(() => {
    const tile = (hue, letter) => 'data:image/svg+xml,' + encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="10" fill="hsl(${hue} 55% 42%)"/><text x="32" y="43" font-size="30" text-anchor="middle" fill="white" font-family="sans-serif" font-weight="700">${letter}</text></svg>`)
    const packs = [['Fabulously Optimized', 'robotkoer', 9800000, 'Improve your graphics and performance with this simple modpack.'], ['Cobblemon Official Modpack', 'Cobblemon', 4100000, 'The official Cobblemon modpack: Pokémon in Minecraft.'],
      ['Better MC [FORGE]', 'SharkieFPS', 3700000, 'Better Minecraft is a modpack that adds hundreds of improvements to vanilla.'], ['Create: Above and Beyond', 'simibubi', 2900000, 'Explore, automate and ascend with Create in an industrial adventure.'],
      ['Prominence II', 'Eliteguy', 1600000, 'An action RPG modpack with bosses, dungeons and loot.'], ['Simply Optimized', 'Rotgruengelb', 1200000, 'A performance-focused modpack that keeps the vanilla feel.']]
    window.fixture.modpackIcons = Object.fromEntries(packs.map(([title], i) => ['p' + i, tile(i * 55, title[0])]))
    window.fixture.modpacks = { total: 4800, hits: packs.map(([title, author, downloads, description], i) => ({ id: 'p' + i, slug: 'pack' + i, title, author, downloads, description, minecraft: '1.21.1', categories: ['optimization', 'technology'].slice(0, 1 + i % 2), hasIcon: true })) }
    window.notify()
  })
  await page.locator('[data-action=browse-modpacks]').click()
  await page.locator('.modpack-card').first().waitFor()
  await page.locator('.modpack-card').first().click()
  await page.evaluate(() => {
    window.fixture.modpacks = { ...window.fixture.modpacks, project: 'p0', versions: [
      { id: 'v3', name: 'FO 7.0 beta', number: '7.0.0-beta.3', type: 'beta', minecraft: ['1.21.4'], loaders: ['fabric'], published: '', size: 1 },
      { id: 'v2', name: 'FO 6.4', number: '6.4.0', type: 'release', minecraft: ['1.21.1'], loaders: ['fabric'], published: '', size: 1 },
      { id: 'v1', name: 'FO 6.3', number: '6.3.2', type: 'release', minecraft: ['1.21'], loaders: ['fabric'], published: '', size: 1 } ] }
    window.notify()
  })
  await page.locator('.modpack-releases button').first().waitFor()
  await page.waitForTimeout(300)
  await shoot('modpacks')
  // B/Close steps back one level at a time: first out of the chosen pack, then out of the page.
  while (await page.locator('.modal-scrim').count()) await page.locator('[data-action=close-dialog]').click()
  await page.locator('.bp-nav-btn').nth(2).click()
  await page.waitForTimeout(200)
  await shoot('settings-tab')
  await page.locator('[data-action=launcher-settings]').click()
  await page.locator('.launcher-settings [data-option=soundVolume]').waitFor()
  await page.waitForTimeout(200)
  await shoot('launcher-settings')
  await page.locator('.dialog-body').evaluate(e => { e.scrollTop = 2000 })
  await shoot('launcher-settings-scrolled')
  await page.locator('[data-action=close-dialog]').click()
  await page.locator('.bp-nav-btn').nth(0).click()
  await page.getByRole('button', { name: /^(Worlds|Mundos)$/ }).first().click()
  await page.locator('[role=tab]').nth(1).click()
  await page.locator('[data-action=remove-server]').waitFor()
  await shoot('servers')
  await page.locator('[data-action=edit-server]').click()
  await page.locator('.server-form').waitFor()
  await shoot('server-edit')
  await page.locator('.server-form button[type=button]').click()
  await page.locator('[data-action=remove-server]').click()
  await shoot('server-remove')
  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > innerWidth)
  console.log(JSON.stringify({ lang, errors, overflow, textOverflow: overflows }, null, 1))
} finally { await browser.close(); server.close() }
