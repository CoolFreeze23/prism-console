// @vitest-environment happy-dom
import { mount, flushPromises } from '@vue/test-utils'
import { describe, expect, it, vi } from 'vitest'
import ModUpdates from './ModUpdates.vue'
const mocks = vi.hoisted(() => ({ invoke: vi.fn() }))
vi.mock('./bridge', () => mocks)
const update = (name: string) => ({ name, from: '1.0', to: '1.1', beta: false, changelog: '' })
describe('mod updates', () => {
  it('updates every mod by default and only the ones left switched on', async () => {
    mocks.invoke.mockResolvedValue(undefined)
    const state = { instance: 'cc', phase: 'ready' as const, total: 212, checked: 140, managed: true, updates: [update('Sodium'), update('Lithium'), update('Iris')] }
    const wrapper = mount(ModUpdates, { props: { instanceId: 'cc', state, busy: false, running: false } })
    expect(wrapper.text()).toContain('3 updates available')
    expect(wrapper.text()).toContain('Checked 140 of 212 mods; the rest aren\'t on Modrinth')
    expect(wrapper.text()).toContain('installed from a modpack')
    expect(wrapper.find('button.primary').text()).toBe('Update all mods')
    await wrapper.findAll('[role=switch]')[1].trigger('click')
    expect(wrapper.find('button.primary').text()).toBe('Update 2 mods')
    await wrapper.find('button.primary').trigger('click'); await flushPromises()
    expect(mocks.invoke).toHaveBeenCalledWith('applyModUpdates', { id: 'cc', names: ['Sodium', 'Iris'] })
    wrapper.unmount()
  })
  it('ignores results that belong to another instance', () => {
    const wrapper = mount(ModUpdates, { props: { instanceId: 'other', state: { instance: 'cc', phase: 'ready', updates: [update('Sodium')] }, busy: false, running: false } })
    expect(wrapper.findAll('[role=switch]')).toHaveLength(0)
    expect(wrapper.find('button').text()).toBe('Check for updates')
    wrapper.unmount()
  })
})
