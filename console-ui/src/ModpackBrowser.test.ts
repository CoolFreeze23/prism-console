// @vitest-environment happy-dom
import { mount, flushPromises } from '@vue/test-utils'
import { afterEach, describe, expect, it, vi } from 'vitest'
import ModpackBrowser from './ModpackBrowser.vue'
const mocks = vi.hoisted(() => ({ invoke: vi.fn() }))
vi.mock('./bridge', () => mocks)
vi.mock('./upstream/gamepad', () => ({ useGamepadAction: vi.fn(), useGamepadInnerNav: vi.fn(), beginTextEntry: vi.fn(), useGamepad: () => ({ registerContext() {}, unregisterContext() {}, labels: { value: {} } }) }))
const hit = { id: 'proj1', slug: 'fabulous', title: 'Fabulously Optimized', description: 'Fast', author: 'robotkoer', downloads: 1234567, minecraft: '1.21.1' }
const releases = [
  { id: 'beta', name: 'b', number: '7.0-beta', type: 'beta', minecraft: ['1.21.4'], loaders: ['fabric'], published: '', size: 1 },
  { id: 'rel', name: 'r', number: '6.4', type: 'release', minecraft: ['1.21.1'], loaders: ['fabric'], published: '', size: 1 },
]
const searches = () => mocks.invoke.mock.calls.filter(c => c[0] === 'searchModpacks')
describe('Modrinth modpack browser', () => {
  afterEach(() => { vi.useRealTimers(); mocks.invoke.mockReset() })
  it('previews a pack on the first tap and installs its newest full release on the next', async () => {
    vi.useFakeTimers()
    mocks.invoke.mockResolvedValue(undefined)
    const wrapper = mount(ModpackBrowser, { props: { busy: false, state: { hits: [hit], total: 1 } } })
    await wrapper.find('.modpack-card').trigger('click')
    vi.advanceTimersByTime(300)
    expect(mocks.invoke).toHaveBeenCalledWith('modpackVersions', { project: 'proj1' })
    expect(mocks.invoke.mock.calls.some(c => c[0] === 'installModpack')).toBe(false)
    await wrapper.setProps({ state: { hits: [hit], total: 1, project: 'proj1', versions: releases } })
    expect(wrapper.find('.preview-target').text()).toBe('Installs 6.4 · Fabric · Minecraft 1.21.1')
    await wrapper.find('.modpack-card').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenLastCalledWith('installModpack', { project: 'proj1', version: 'rel', name: 'Fabulously Optimized' })
    expect(wrapper.emitted('installed')).toHaveLength(1)
    wrapper.unmount()
  })
  it('installs as soon as the releases arrive when asked before they did', async () => {
    mocks.invoke.mockResolvedValue(undefined)
    const wrapper = mount(ModpackBrowser, { props: { busy: false, state: { hits: [hit], total: 1 } } })
    await wrapper.find('.modpack-card').trigger('click')
    await wrapper.find('.modpack-card').trigger('click'); await flushPromises()
    expect(mocks.invoke.mock.calls.some(c => c[0] === 'installModpack')).toBe(false)
    await wrapper.setProps({ state: { hits: [hit], total: 1, project: 'proj1', versions: releases } }); await flushPromises()
    expect(mocks.invoke).toHaveBeenLastCalledWith('installModpack', { project: 'proj1', version: 'rel', name: 'Fabulously Optimized' })
    wrapper.unmount()
  })
  it('installs the release that fits the version filter, or the one picked with Other version', async () => {
    mocks.invoke.mockResolvedValue(undefined)
    const state = { hits: [hit], total: 1, filters: { loader: '', gameVersion: '1.21.4', sort: '' } }
    const wrapper = mount(ModpackBrowser, { props: { busy: false, state }, attachTo: document.body })
    await wrapper.find('.modpack-card').trigger('click')
    await wrapper.setProps({ state: { ...state, project: 'proj1', versions: releases } })
    expect(wrapper.find('.preview-target').text()).toContain('Installs 7.0-beta')
    await wrapper.find('[data-action=other-version]').trigger('click'); await flushPromises()
    await wrapper.find('.modpack-picker [data-value=rel]').trigger('click'); await flushPromises()
    expect(wrapper.find('.modpack-picker').exists()).toBe(false)
    await wrapper.find('[data-action=install-modpack]').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenLastCalledWith('installModpack', { project: 'proj1', version: 'rel', name: 'Fabulously Optimized' })
    wrapper.unmount()
  })
  it('fetches each icon once, retries after an error, and B closes an open picker before leaving', async () => {
    mocks.invoke.mockImplementation(async (method: string) => method === 'modpackIcons' ? { proj1: 'data:image/png;base64,AAA' } : undefined)
    const state = { hits: [{ ...hit, hasIcon: true }], total: 1 }
    const wrapper = mount(ModpackBrowser, { props: { busy: false, state }, attachTo: document.body })
    await flushPromises()
    await wrapper.setProps({ state: { ...state, loading: false } }); await flushPromises()
    expect(mocks.invoke.mock.calls.filter(c => c[0] === 'modpackIcons')).toEqual([['modpackIcons', { ids: ['proj1'] }]])
    expect(wrapper.find('.modpack-card img').attributes('src')).toBe('data:image/png;base64,AAA')
    await wrapper.setProps({ state: { ...state, error: "Can't reach Modrinth." } })
    await wrapper.find('.modpack-error button').trigger('click')
    expect(mocks.invoke).toHaveBeenLastCalledWith('searchModpacks', { query: '', offset: 0, loader: '', gameVersion: '', sort: '' })
    await wrapper.setProps({ state })
    await wrapper.find('[data-filter=loader]').trigger('click'); await flushPromises()
    expect(wrapper.find('.modpack-picker').exists()).toBe(true)
    expect(window.dispatchEvent(new Event('prism-back', { cancelable: true }))).toBe(false)
    await flushPromises()
    expect(wrapper.find('.modpack-picker').exists()).toBe(false)
    expect(window.dispatchEvent(new Event('prism-back', { cancelable: true }))).toBe(true)
    wrapper.unmount()
  })
  it('moves a controller to the first result when the results arrive after the page opened', async () => {
    mocks.invoke.mockResolvedValue(undefined)
    const wrapper = mount(ModpackBrowser, { props: { busy: false, state: { loading: true } }, attachTo: document.body })
    ;(wrapper.find('.modpack-search input').element as HTMLElement).focus()
    await wrapper.setProps({ state: { hits: [hit], total: 1 } }); await flushPromises()
    expect(document.activeElement?.classList.contains('modpack-card')).toBe(true)
    wrapper.unmount()
  })
  it('filters by loader, Minecraft version and sort order, and clears them when nothing matches', async () => {
    mocks.invoke.mockResolvedValue(undefined)
    const wrapper = mount(ModpackBrowser, { props: { busy: false, state: { hits: [hit], total: 1, gameVersions: ['1.21.4', '1.21.1', '1.20.1'] } }, attachTo: document.body })
    expect(wrapper.find('[data-filter=sort]').text()).toContain('Popular')  // popular without a query
    const choose = async (filter: string, value: string) => {
      await wrapper.find(`[data-filter=${filter}]`).trigger('click'); await flushPromises()
      await wrapper.find(`.modpack-picker [data-value="${value}"]`).trigger('click'); await flushPromises()
    }
    await choose('loader', 'neoforge')
    expect(searches().at(-1)?.[1]).toEqual({ query: '', offset: 0, loader: 'neoforge', gameVersion: '', sort: '' })
    await choose('version', '1.21.1')
    expect(searches().at(-1)?.[1]).toEqual({ query: '', offset: 0, loader: 'neoforge', gameVersion: '1.21.1', sort: '' })
    await choose('sort', 'newest')
    expect(searches().at(-1)?.[1].sort).toBe('newest')
    await wrapper.setProps({ state: { hits: [], total: 0, loading: false } })
    expect(wrapper.text()).toContain('No modpacks match these filters')
    await wrapper.find('[data-action=clear-filters]').trigger('click')
    expect(searches().at(-1)?.[1]).toEqual({ query: '', offset: 0, loader: '', gameVersion: '', sort: '' })
    wrapper.unmount()
  })
})
