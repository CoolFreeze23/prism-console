// @vitest-environment happy-dom
import { mount, flushPromises } from '@vue/test-utils'
import { describe, expect, it, vi } from 'vitest'
import ModpackBrowser from './ModpackBrowser.vue'
const mocks = vi.hoisted(() => ({ invoke: vi.fn() }))
vi.mock('./bridge', () => mocks)
const hit = { id: 'proj1', slug: 'fabulous', title: 'Fabulously Optimized', description: 'Fast', author: 'robotkoer', downloads: 1234567, minecraft: '1.21.1' }
describe('Modrinth modpack browser', () => {
  it('installs the newest full release of the chosen pack under the chosen name', async () => {
    mocks.invoke.mockResolvedValue(undefined)
    const wrapper = mount(ModpackBrowser, { props: { busy: false, state: { hits: [hit], total: 1 } } })
    await wrapper.find('.modpack-card').trigger('click')
    expect(mocks.invoke).toHaveBeenCalledWith('modpackVersions', { project: 'proj1' })
    await wrapper.setProps({ state: { hits: [hit], total: 1, project: 'proj1', versions: [
      { id: 'beta', name: 'b', number: '7.0-beta', type: 'beta', minecraft: ['1.21.1'], loaders: ['fabric'], published: '', size: 1 },
      { id: 'rel', name: 'r', number: '6.4', type: 'release', minecraft: ['1.21.1'], loaders: ['fabric'], published: '', size: 1 },
    ] } })
    expect(wrapper.findAll('[role=radio]').map(r => r.attributes('aria-checked'))).toEqual(['false', 'true'])
    expect(wrapper.find('.modpack-releases').text()).toContain('Minecraft 1.21.1 · Fabric')
    await wrapper.find('.setting-field input').setValue('FO handheld')
    await wrapper.find('.modpack-install').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenLastCalledWith('installModpack', { project: 'proj1', version: 'rel', name: 'FO handheld' })
    expect(wrapper.emitted('installed')).toHaveLength(1)
    wrapper.unmount()
  })
  it('fetches each icon once, retries after an error, and B steps back to the results first', async () => {
    mocks.invoke.mockReset()
    mocks.invoke.mockImplementation(async (method: string) => method === 'modpackIcons' ? { proj1: 'data:image/png;base64,AAA' } : undefined)
    const state = { hits: [{ ...hit, hasIcon: true }], total: 1 }
    const wrapper = mount(ModpackBrowser, { props: { busy: false, state }, attachTo: document.body })
    await flushPromises()
    await wrapper.setProps({ state: { ...state, loading: false } }); await flushPromises()
    expect(mocks.invoke.mock.calls.filter(c => c[0] === 'modpackIcons')).toEqual([['modpackIcons', { ids: ['proj1'] }]])
    expect(wrapper.find('.modpack-card img').attributes('src')).toBe('data:image/png;base64,AAA')
    await wrapper.setProps({ state: { ...state, error: "Can't reach Modrinth." } })
    await wrapper.find('.modpack-error button').trigger('click')
    expect(mocks.invoke).toHaveBeenLastCalledWith('searchModpacks', { query: '', offset: 0 })
    await wrapper.find('.modpack-card').trigger('click')
    expect(wrapper.find('.modpack-detail').exists()).toBe(true)
    const back = new Event('prism-back', { cancelable: true })
    expect(window.dispatchEvent(back)).toBe(false)
    await flushPromises()
    expect(wrapper.find('.modpack-detail').exists()).toBe(false)
    expect(window.dispatchEvent(new Event('prism-back', { cancelable: true }))).toBe(true)
    wrapper.unmount()
  })
})
