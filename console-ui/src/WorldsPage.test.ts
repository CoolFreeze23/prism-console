// @vitest-environment happy-dom
import { mount, flushPromises } from '@vue/test-utils'
import { describe, expect, it, vi } from 'vitest'
import WorldsPage from './WorldsPage.vue'
const mocks = vi.hoisted(() => ({ invoke: vi.fn() }))
vi.mock('./bridge', () => mocks)
describe('worlds and screenshots', () => {
  it('starts on the most recently played world and plays the chosen one', async () => {
    mocks.invoke.mockImplementation(async (method: string) => method === 'worlds' ? [
      { id: 'Old', name: 'Old base', mode: 'Survival', lastPlayedMs: 1000, size: 5e6, icon: '' },
      { id: 'New', name: 'New Island', mode: 'Creative', lastPlayedMs: Date.now(), size: 2e8, icon: '' },
    ] : method === 'servers' ? [{ name: 'Hypixel', address: 'mc.hypixel.net', icon: '' }] : method === 'screenshots' ? { total: 1, shots: [{ name: 'a.png', taken: Date.now(), thumb: 'data:image/jpeg;base64,' }] } : undefined)
    const wrapper = mount(WorldsPage, { props: { instanceId: 'cc', running: false, busy: false } })
    await flushPromises()
    expect(wrapper.findAll('.world-row .setting-text > span').map(r => r.text())).toEqual(['New Island', 'Old base'])
    expect(wrapper.find('.world-detail h2').text()).toBe('New Island')
    await wrapper.findAll('.world-row')[1].trigger('click')
    await wrapper.find('.world-play').trigger('click')
    expect(wrapper.emitted('play')?.[0]).toEqual([{ world: 'Old' }])
    await wrapper.findAll('[role=tab]')[1].trigger('click'); await flushPromises()
    await wrapper.find('.world-play').trigger('click')
    expect(wrapper.emitted('play')?.[1]).toEqual([{ server: 'mc.hypixel.net' }])
    await wrapper.findAll('[role=tab]')[2].trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('screenshots', { id: 'cc', offset: 0 })
    expect(wrapper.findAll('.shot')).toHaveLength(1)
    wrapper.unmount()
  })

  it('adds, edits and removes servers by position, with a confirm step for removal', async () => {
    let list = [{ name: 'Hypixel', address: 'mc.hypixel.net', icon: '' }, { name: 'Twin', address: 'mc.hypixel.net', icon: '' }]
    mocks.invoke.mockReset()
    mocks.invoke.mockImplementation(async (method: string, args: Record<string, unknown>) => {
      if (method === 'worlds') return []
      if (method === 'servers') return list
      if (method === 'saveServer') {
        const entry = { name: String(args.name), address: String(args.address), icon: '' }
        list = Number(args.index) < 0 ? [...list, entry] : list.map((s, i) => i === args.index ? entry : s)
        return list
      }
      if (method === 'removeServer') { list = list.filter((_, i) => i !== args.index); return list }
    })
    const wrapper = mount(WorldsPage, { props: { instanceId: 'cc', running: false, busy: false }, attachTo: document.body })
    await flushPromises()
    await wrapper.findAll('[role=tab]')[1].trigger('click'); await flushPromises()

    await wrapper.find('[data-action=add-server]').trigger('click')
    await wrapper.find('[data-field=name]').setValue(' Home ')
    await wrapper.find('[data-field=address]').setValue('192.168.8.20:25565')
    await wrapper.find('.server-form').trigger('submit'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('saveServer', { id: 'cc', index: -1, expected: '', name: 'Home', address: '192.168.8.20:25565' })
    expect(wrapper.find('.world-detail h2').text()).toBe('Home')

    // Same address twice: the second entry is edited, identified by its position.
    await wrapper.findAll('.world-row')[1].trigger('click')
    await wrapper.find('[data-action=edit-server]').trigger('click')
    expect((wrapper.find('[data-field=name]').element as HTMLInputElement).value).toBe('Twin')
    await wrapper.find('[data-field=name]').setValue('Twin lobby')
    await wrapper.find('.server-form').trigger('submit'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('saveServer', { id: 'cc', index: 1, expected: 'mc.hypixel.net', name: 'Twin lobby', address: 'mc.hypixel.net' })

    // B backs out of the removal question without removing anything.
    await wrapper.find('[data-action=remove-server]').trigger('click')
    const back = new Event('prism-back', { cancelable: true }); window.dispatchEvent(back)
    expect(back.defaultPrevented).toBe(true)
    await flushPromises()
    expect(wrapper.find('[data-action=confirm-remove-server]').exists()).toBe(false)
    await wrapper.find('[data-action=remove-server]').trigger('click')
    await wrapper.find('[data-action=confirm-remove-server]').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('removeServer', { id: 'cc', index: 1, expected: 'mc.hypixel.net' })
    expect(wrapper.findAll('.world-row').map(r => r.find('.setting-text > span').text())).toEqual(['Hypixel', 'Home'])
    wrapper.unmount()
  })

  it('locks the server list while the game runs', async () => {
    mocks.invoke.mockImplementation(async (method: string) => method === 'servers' ? [{ name: 'Hypixel', address: 'mc.hypixel.net', icon: '' }] : [])
    const wrapper = mount(WorldsPage, { props: { instanceId: 'cc', running: true, busy: false } })
    await flushPromises()
    await wrapper.findAll('[role=tab]')[1].trigger('click'); await flushPromises()
    for (const action of ['add-server', 'edit-server', 'remove-server']) expect(wrapper.find(`[data-action=${action}]`).attributes('disabled')).toBeDefined()
    wrapper.unmount()
  })
})
