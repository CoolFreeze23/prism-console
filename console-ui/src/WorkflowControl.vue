<script setup lang="ts">
import { t } from './i18n'
import { computed, ref, watch } from 'vue'
import SkinPreview from './SkinPreview.vue'
export type Control = { id: string; type: string; label?: string; text?: string; hint?: string; role?: 'radio' | 'check' | 'toggle'; group?: string; value?: any; checked?: boolean; enabled?: boolean; readonly?: boolean; secret?: boolean; placeholder?: string; maxLength?: number; min?: number; max?: number; step?: number; suffix?: string; image?: string; children?: Control[]; options?: any[]; rows?: any[]; links?: { text: string; url: string }[]; editable?: boolean; checkable?: boolean; offset?: number; total?: number; more?: boolean; multi?: boolean; context?: boolean; menu?: boolean; sidebar?: boolean; skin?: string; slim?: boolean; cape?: string; elytra?: boolean }
const props = defineProps<{ node: Control }>()
const emit = defineEmits<{ action: [args: Record<string, unknown>] }>()
const submitted = ref<unknown>()
const local = ref(props.node.value), editing = ref(false), expanded = ref(false), filter = ref(''), multi = ref(false)
watch(() => props.node.value, value => { if (!editing.value) local.value = value })
const options = computed(() => (props.node.options || []).filter(o => !filter.value || o.text.toLowerCase().includes(filter.value.toLowerCase())))
const current = computed(() => props.node.options?.find(o => o.index === props.node.value)?.text || t('common.choose'))
// Consecutive radio buttons of one exclusive group collapse into a single segmented choice.
type Block = { key: string; node?: Control; radios?: Control[] }
const blocks = computed(() => {
  const out: Block[] = []
  for (const child of props.node.children || []) {
    const last = out[out.length - 1]
    if (child.type === 'button' && child.role === 'radio' && child.group) {
      if (last?.radios && last.radios[0].group === child.group) last.radios.push(child)
      else out.push({ key: 'radios-' + child.id, radios: [child] })
    } else out.push({ key: child.id, node: child })
  }
  return out
})
const heading = computed(() => { const text = props.node.text || ''; return text.length < 60 && !text.includes('\n') })
function act(op: string, args: Record<string, unknown> = {}) { emit('action', { control: props.node.id, op, ...args }) }
function commit() { editing.value = false; if (local.value !== props.node.value && local.value !== submitted.value) { submitted.value = local.value; act('value', { value: local.value }); if (props.node.secret) local.value = '' } }
</script>
<template>
  <div class="workflow-control" :class="['control-' + node.type, { 'control-toggle': node.role && node.role !== 'radio' }]" :data-control="node.id">
    <template v-if="node.type === 'button' && node.role && node.role !== 'radio'">
      <button class="setting-toggle" role="switch" :aria-checked="!!node.checked" :disabled="!node.enabled" @click="act('click')">
        <span class="setting-text"><span>{{ node.text || node.label }}</span><small v-if="node.hint">{{ node.hint }}</small></span>
        <span class="switch" aria-hidden="true"><span /></span>
      </button>
    </template>
    <template v-else-if="node.type === 'button'"><button :disabled="!node.enabled" :role="node.role === 'radio' ? 'radio' : undefined" :aria-checked="node.role === 'radio' ? !!node.checked : undefined" @click="act('click')"><img v-if="node.image" :src="node.image" alt="">{{ node.text || node.label || t('common.open') }}</button><button v-if="node.menu" :disabled="!node.enabled" @click="act('menu')">{{ t('control.more_options') }}</button></template>
    <template v-else-if="node.type === 'label'"><p v-if="node.text" :class="{ 'label-heading': heading }">{{ node.text }}</p><img v-if="node.image" :src="node.image" :alt="node.label || ''" class="workflow-image"><button v-for="(link, i) in node.links" :key="i" @click="act('link', { value: link.url })">{{ link.text || link.url }}</button></template>
    <template v-else-if="node.type === 'input' || node.type === 'number'"><label class="setting-field"><span>{{ node.label }}<small v-if="node.suffix"> {{ node.suffix.trim() }}</small></span><input v-model="local" :type="node.secret ? 'password' : node.type === 'number' ? 'number' : 'text'" :disabled="!node.enabled" :readonly="node.readonly" :min="node.min" :max="node.max" :step="node.step" :maxlength="node.maxLength" :placeholder="node.placeholder" @focus="editing = true; submitted = undefined" @change="commit" @blur="commit" @keydown.enter="commit(); node.type === 'input' && act('enter')"></label></template>
    <template v-else-if="node.type === 'text'"><label>{{ node.label }}<textarea v-if="!node.readonly" v-model="local" rows="8" :disabled="!node.enabled" @focus="editing = true; submitted = undefined" @change="commit" @blur="commit"/><pre v-else tabindex="0" class="workflow-text">{{ node.value }}</pre></label><button v-for="(link, i) in node.links" :key="i" @click="act('link', { value: link.url })">{{ link.text || link.url }}</button></template>
    <template v-else-if="node.type === 'progress'"><p role="status">{{ node.text }}</p><progress :value="node.max ? node.value : undefined" :max="node.max || 1" /></template>
    <template v-else-if="node.type === 'select'"><label v-if="node.editable" class="setting-field"><span>{{ node.label }}</span><input :value="node.text" @change="act('value', { value: ($event.target as HTMLInputElement).value })"></label><button class="setting-select" :disabled="!node.enabled" :aria-expanded="expanded" @click="expanded = !expanded"><span>{{ node.label }}</span><span class="setting-value">{{ current }} <span aria-hidden="true">{{ expanded ? '▴' : '▾' }}</span></span></button><div v-if="expanded" class="workflow-options"><input v-if="(node.options?.length || 0) > 12" v-model="filter" :aria-label="t('control.filter_choices')" :placeholder="t('control.filter_choices')"><button v-for="o in options" :key="o.index" class="row" :disabled="!node.enabled || o.enabled === false" :aria-pressed="node.value === o.index" @click="act('select', { value: o.index }); expanded = false">{{ o.text }}</button></div></template>
    <template v-else-if="node.type === 'tabs'"><nav class="actions" :aria-label="node.label || 'Sections'"><button v-for="o in node.options" :key="o.index" :disabled="!o.enabled" :aria-pressed="node.value === o.index" @click="act('select', { value: o.index })">{{ o.text }}</button></nav><WorkflowControl v-for="child in node.children" :key="child.id" :node="child" @action="emit('action', $event)" /></template>
    <template v-else-if="node.type === 'menu'"><button v-for="o in node.options" :key="o.id" class="row" :disabled="!o.enabled" @click="emit('action', { control: o.id, op: 'click' })">{{ o.text }}</button></template>
    <template v-else-if="node.type === 'list'">
      <h3 v-if="node.label">{{ node.label }}</h3>
      <div class="workflow-list"><div v-for="r in node.rows" :key="r.path.join('-')" class="workflow-row" :style="{ paddingLeft: `${r.depth * 16}px` }">
        <button v-if="r.children" :aria-expanded="r.expanded" :aria-label="r.expanded ? t('control.collapse', { name: r.text }) : t('control.expand', { name: r.text })" @click="act('expand', { path: r.path, expected: r.text })">{{ r.expanded ? '−' : '+' }}</button>
        <button class="row" :disabled="!r.enabled" :aria-pressed="r.selected" @click="act('select', { path: r.path, expected: r.text, multi })"><img v-if="r.image" :src="r.image" alt=""><span class="row-text">{{ r.text || t('control.unnamed') }}</span></button>
        <button v-if="r.checked !== undefined" class="mini-switch" role="switch" :disabled="!r.enabled" :aria-checked="r.checked" :aria-label="t('control.enable', { name: r.text })" @click="act('check', { path: r.path, expected: r.text, value: !r.checked })"><span class="switch" aria-hidden="true"><span /></span></button>
        <template v-if="r.selected && !node.sidebar"><button @click="act('activate', { path: r.path, expected: r.text })">{{ t('control.open') }}</button><button v-if="node.context" @click="act('context', { path: r.path, expected: r.text })">{{ t('control.actions') }}</button><input v-if="r.editable" :value="r.editValue ?? r.text" :aria-label="t('control.rename_selected_item')" @change="act('edit', { path: r.path, expected: r.text, value: ($event.target as HTMLInputElement).value })"></template>
      </div></div>
      <div class="actions"><button v-if="node.multi" class="setting-toggle" role="switch" :aria-checked="multi" @click="multi = !multi"><span class="setting-text"><span>{{ t('control.multiSelect') }}</span></span><span class="switch" aria-hidden="true"><span /></span></button><button v-if="node.offset" @click="act('page', { value: Math.max(0, (node.offset || 0) - 200) })">{{ t('control.previous_items') }}</button><button v-if="(node.total || 0) > (node.offset || 0) + 200" @click="act('page', { value: (node.offset || 0) + 200 })">{{ t('control.next_items') }}</button><button v-if="node.more" @click="act('fetch')">{{ t('control.load_more') }}</button></div>
    </template>
    <template v-else-if="node.type === 'pages'"><aside class="workflow-sidebar"><WorkflowControl v-for="child in node.children?.filter(c => c.sidebar)" :key="child.id" :node="child" @action="emit('action', $event)" /></aside><section class="workflow-page"><WorkflowControl v-for="child in node.children?.filter(c => !c.sidebar)" :key="child.id" :node="child" @action="emit('action', $event)" /></section></template>
    <template v-else>
      <SkinPreview v-if="node.skin" :skin="node.skin" :slim="node.slim" :cape="node.cape" :elytra="node.elytra" />
      <button v-if="node.checkable" class="setting-toggle" role="switch" :aria-checked="!!node.checked" :disabled="!node.enabled" @click="act('click')"><span class="setting-text"><span>{{ node.label }}</span></span><span class="switch" aria-hidden="true"><span /></span></button>
      <h3 v-else-if="node.label && !node.children?.some(c => c.label === node.label)">{{ node.label }}</h3>
      <template v-for="block in blocks" :key="block.key">
        <div v-if="block.radios" class="workflow-control control-segmented" role="radiogroup">
          <button v-for="radio in block.radios" :key="radio.id" role="radio" :data-control="radio.id" :aria-checked="!!radio.checked" :disabled="!radio.enabled" :title="radio.hint" @click="emit('action', { control: radio.id, op: 'click' })">{{ radio.text }}</button>
        </div>
        <WorkflowControl v-else :node="block.node!" @action="emit('action', $event)" />
      </template>
    </template>
  </div>
</template>
