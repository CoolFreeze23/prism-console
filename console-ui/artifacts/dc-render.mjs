// Renders design-canvas artboards (.dc.html) to PNG for review: fills {{holes}}, <sc-for> and <sc-if> from the
// artboard's renderVals(), then screenshots it at its preview size. Usage: node dc-render.mjs <out-dir> <file.dc.html>...
import { chromium } from 'playwright-core'
import { readFile, mkdir } from 'node:fs/promises'
import { basename } from 'node:path'

const [outDir, ...files] = process.argv.slice(2)
await mkdir(outDir, { recursive: true })
const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe', headless: true })
try {
  for (const file of files) {
    const source = await readFile(file, 'utf8')
    const props = JSON.parse((source.match(/data-props='([^']*)'/) || [, '{}'])[1].replace(/&amp;/g, '&').replace(/&#39;/g, "'"))
    const size = props.$preview || { width: 1280, height: 800 }
    const page = await browser.newPage({ viewport: { width: size.width, height: size.height } })
    await page.setContent(source.replace(/<script src="\.\/support\.js"><\/script>/, ''), { waitUntil: 'domcontentloaded' })
    await page.evaluate(() => {
      class DCLogic { constructor() { this.props = {}; this.state = {} } setState() {} }
      const code = document.querySelector('script[data-dc-script]').textContent
      const Component = new Function('DCLogic', code + '; return Component')(DCLogic)
      const vals = new Component().renderVals()
      const root = document.querySelector('x-dc')
      for (const helmet of root.querySelectorAll('helmet')) { for (const child of [...helmet.children]) document.head.append(child); helmet.remove() }
      const lookup = (path, scope) => {
        path = path.trim()
        if (path === 'true') return true
        if (path === 'false') return false
        if (/^-?\d+(\.\d+)?$/.test(path)) return Number(path)
        return path.split('.').reduce((value, key) => value?.[key], scope)
      }
      const fill = (text, scope) => text.replace(/\{\{([^}]+)\}\}/g, (_, path) => { const v = lookup(path, scope); return v == null ? '' : String(v) })
      const render = (node, scope, parent) => {
        if (node.nodeType === 3) { parent.append(document.createTextNode(fill(node.textContent, scope))); return }
        if (node.nodeType !== 1) return
        const tag = node.tagName
        if (tag === 'SC-FOR') {
          const list = lookup(node.getAttribute('list').replace(/[{}]/g, ''), scope) || []
          const as = node.getAttribute('as')
          list.forEach((item, index) => { for (const child of node.childNodes) render(child, { ...scope, [as]: item, $index: index }, parent) })
          return
        }
        if (tag === 'SC-IF') {
          if (lookup(node.getAttribute('value').replace(/[{}]/g, ''), scope)) for (const child of node.childNodes) render(child, scope, parent)
          return
        }
        const copy = document.createElementNS(node.namespaceURI, node.localName)
        for (const attr of node.attributes) copy.setAttribute(attr.name, fill(attr.value, scope))
        for (const child of node.childNodes) render(child, scope, copy)
        parent.append(copy)
      }
      const out = document.createElement('div')
      for (const child of [...root.childNodes]) render(child, vals, out)
      root.replaceWith(...out.childNodes)
    })
    await page.evaluate(() => document.fonts.ready)
    await page.waitForTimeout(600)
    const name = basename(file).replace('.dc.html', '.png')
    await page.screenshot({ path: `${outDir}/${name}` })
    console.log('rendered', name)
    await page.close()
  }
} finally { await browser.close() }
