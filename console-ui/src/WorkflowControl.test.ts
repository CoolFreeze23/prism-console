// @vitest-environment happy-dom
import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import WorkflowControl from './WorkflowControl.vue'
describe('controller workflow controls', () => {
  it('includes the visible row identity when selecting asynchronous results', async () => {
    const wrapper = mount(WorkflowControl, { props: { node: { id: 'list', type: 'list', enabled: true, rows: [{ path: [2], text: 'Chosen pack', enabled: true, selected: false, depth: 0 }] } } })
    await wrapper.find('button').trigger('click')
    expect(wrapper.emitted('action')?.[0]).toEqual([{ control: 'list', op: 'select', path: [2], expected: 'Chosen pack', multi: false }])
    wrapper.unmount()
  })
  it('does not submit unchanged masked secrets or submit the same edit twice', async () => {
    const wrapper = mount(WorkflowControl, { props: { node: { id: 'secret', type: 'input', enabled: true, secret: true, value: '', placeholder: 'Saved value' } } })
    await wrapper.find('input').trigger('blur')
    expect(wrapper.emitted('action')).toBeUndefined()
    await wrapper.find('input').setValue('fixture-only')
    await wrapper.find('input').trigger('blur')
    expect(wrapper.emitted('action')).toHaveLength(1)
    expect((wrapper.find('input').element as HTMLInputElement).value).toBe('')
    wrapper.unmount()
  })
  it('renders grouped and tabbed actions without exposing disabled operations', async () => {
    const wrapper = mount(WorkflowControl, { props: { node: { id: 'tabs', type: 'tabs', value: 0, options: [{ index: 0, text: 'Current', enabled: true }, { index: 1, text: 'Unavailable', enabled: false }], children: [{ id: 'save', type: 'button', text: 'Save', enabled: true }] } } })
    expect(wrapper.findAll('button')[1].attributes('disabled')).toBeDefined()
    await wrapper.findAll('button')[2].trigger('click')
    expect(wrapper.emitted('action')?.[0]).toEqual([{ control: 'save', op: 'click' }])
    wrapper.unmount()
  })
  it('groups exclusive radio buttons into one segmented choice and renders checkboxes as switches', async () => {
    const radio = (id: string, text: string, checked: boolean) => ({ id, type: 'button', role: 'radio' as const, group: 'sort', text, checked, enabled: true })
    const wrapper = mount(WorkflowControl, { props: { node: { id: 'group', type: 'group', enabled: true, children: [
      radio('name', 'By name', true), radio('last', 'By last launched', false),
      { id: 'menubar', type: 'button', role: 'check' as const, text: 'Replace toolbar with menubar', hint: 'Friendlier for keyboards', checked: false, enabled: true },
    ] } } })
    expect(wrapper.findAll('[role=radiogroup]')).toHaveLength(1)
    expect(wrapper.findAll('[role=radio]').map(r => r.attributes('aria-checked'))).toEqual(['true', 'false'])
    expect(wrapper.text()).not.toMatch(/[✓○]/)
    await wrapper.findAll('[role=radio]')[1].trigger('click')
    expect(wrapper.emitted('action')?.[0]).toEqual([{ control: 'last', op: 'click' }])
    const toggle = wrapper.find('[role=switch]')
    expect(toggle.attributes('aria-checked')).toBe('false')
    expect(toggle.text()).toContain('Friendlier for keyboards')
    wrapper.unmount()
  })
})
