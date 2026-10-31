// @vitest-environment happy-dom
import { mount } from '@vue/test-utils'
import { describe, expect, it } from 'vitest'
import SkinPreview from './SkinPreview.vue'

describe('controller skin preview', () => {
  it('rotates, toggles the outer layer and bounds zoom using buttons', async () => {
    const w = mount(SkinPreview, { props: { skin: 'data:image/png;base64,fixture' } })
    expect(w.findAll('.skin-face')).toHaveLength(72)
    const button = (text: string) => w.findAll('button').find(b => b.text().startsWith(text))!
    await button('Rotate right').trigger('click')
    expect(w.find('.skin-person').attributes('style')).toContain('rotateY(5deg)')
    await button('Outer layers').trigger('click')
    expect(w.findAll('.skin-face')).toHaveLength(36)
    for (let i = 0; i < 5; i++) await button('Zoom out').trigger('click')
    expect(button('Zoom out').attributes('disabled')).toBeDefined()
    w.unmount()
  })
  it('supports slim arms and switches an owned cape to elytra', async () => {
    const w = mount(SkinPreview, { props: { skin: 'fixture', slim: true, cape: 'cape' } })
    expect(w.findAll('.skin-part')[2].attributes('style')).toContain('width: 15px')
    expect(w.findAll('.skin-cape')).toHaveLength(1)
    await w.setProps({ elytra: true })
    expect(w.findAll('.skin-cape')).toHaveLength(2)
    w.unmount()
  })
})
