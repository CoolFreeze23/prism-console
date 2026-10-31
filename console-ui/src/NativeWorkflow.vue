<script setup lang="ts">
import { nextTick, ref } from 'vue'
import { invoke } from './bridge'
import WorkflowControl, { type Control } from './WorkflowControl.vue'
const props = defineProps<{ flow: { id: string; title: string; content: Control } }>()
const emit = defineEmits<{ refresh: [] }>()
const error = ref('')
let queue = Promise.resolve()
function action(args: Record<string, unknown>) {
  const active = document.activeElement as HTMLElement
  const control = active?.closest('[data-control]')?.getAttribute('data-control')
  queue = queue.then(async () => {
    error.value = ''
    try { await invoke('workflowAction', args); emit('refresh'); await nextTick(); if (!document.activeElement || document.activeElement === document.body) document.querySelector<HTMLElement>(`button[data-control="${control}"], [data-control="${control}"] button, [data-control="${control}"] input`)?.focus() }
    catch (e) { error.value = String(e).replace(/^Error:\s*/, '') }
  })
}
defineExpose({ close: () => action({ control: props.flow.id, op: 'close' }) })
</script>
<template><section class="native-workflow"><p v-if="error" role="alert" class="workflow-error">{{ error }}</p><WorkflowControl :node="flow.content" @action="action" /></section></template>
<style>
.native-workflow { min-width: 0; } .workflow-control { min-width: 0; }
.workflow-control input, .workflow-control textarea { max-width: 100%; box-sizing: border-box; }
.workflow-control label { display: block; margin: 8px 0; }
.workflow-control img { max-width: 100%; } .workflow-control button img { width: 28px; height: 28px; vertical-align: middle; margin-right: 10px; }
.control-group > .control-button { display: inline-block; margin: 4px 8px 4px 0; }
.control-group > .control-button.control-toggle { display: block; margin: 0; }

/* Section headings and captions */
.control-group > h3 { color: var(--pc-text); font-size: 20px; font-weight: 700; letter-spacing: 0; text-transform: none; margin: 28px 0 8px; padding-bottom: 8px; border-bottom: 2px solid #2f3031; }
.control-label > p { margin: 10px 0; color: #d6d8da; overflow-wrap: anywhere; white-space: pre-wrap; line-height: 1.45; }
.control-label > p.label-heading { margin: 18px 0 8px; color: #fff; font-weight: 600; }

/* On/off settings: label on the left, switch on the right */
/* Rows are rounded for the focus ring; the separator is an inset line so it stays straight. */
.setting-toggle, .setting-select { display: flex; align-items: center; justify-content: space-between; gap: 24px; width: 100%; text-align: left; border-image: none !important; border: 0; background: linear-gradient(var(--pc-raised), var(--pc-raised)) left 14px bottom 0 / calc(100% - 28px) 1px no-repeat; background-color: transparent; padding: 12px 16px; min-height: 72px; }
.setting-toggle:hover:not(:disabled), .setting-select:hover:not(:disabled) { background-color: var(--pc-surface); }
.setting-text { display: grid; gap: 4px; min-width: 0; }
.setting-text small { color: var(--ore-muted); font-size: 16px; line-height: 1.35; display: -webkit-box; -webkit-line-clamp: 2; -webkit-box-orient: vertical; overflow: hidden; }
.switch { flex: none; position: relative; width: 58px; height: 34px; border-radius: 17px; background: var(--pc-surface); box-shadow: inset 0 0 0 2px var(--pc-line); }
.switch > span { position: absolute; top: 6px; left: 6px; width: 22px; height: 22px; border-radius: 50%; background: var(--pc-text-2); transition: transform 120ms ease, background-color 120ms ease; }
[aria-checked=true] > .switch { background: var(--pc-text); box-shadow: none; }
[aria-checked=true] > .switch > span { transform: translateX(24px); background: var(--pc-bg); }
.mini-switch { min-height: 44px; padding: 6px 10px; border-image: none !important; border: 0; background: transparent; }

/* Radio groups: one segmented control */
.control-segmented { display: flex; flex-wrap: wrap; gap: 8px; margin: 4px 0 12px; }
.control-segmented > button { min-height: 52px; padding: 8px 22px; border-radius: var(--pc-pill); }
.control-segmented > button[aria-checked=true] { background: var(--pc-text); color: var(--pc-bg); }

/* Fields: caption left, value right */
.setting-field { display: grid !important; grid-template-columns: minmax(0, 1fr) minmax(0, 1.25fr); align-items: center; gap: 20px; padding: 6px 14px; margin: 0 !important; border-bottom: 1px solid var(--pc-raised); }
.setting-field small { color: var(--ore-muted); }
.setting-field input { width: 100%; }
.setting-value { color: var(--ore-muted); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; max-width: 60%; }

/* Lists */
.workflow-text { white-space: pre-wrap; overflow-wrap: anywhere; max-height: 48vh; overflow: auto; font-size: 16px; line-height: 1.5; }
.workflow-list { max-height: 52vh; overflow: auto; }
.workflow-row { display: flex; gap: 6px; flex-wrap: wrap; align-items: center; margin: 5px 0; }
.workflow-row > .row { flex: 1; min-width: 160px; justify-content: flex-start; text-align: left; overflow-wrap: anywhere; }
.workflow-row input { flex-basis: 100%; }
.workflow-options { max-height: 40vh; overflow: auto; padding: 8px; } .workflow-options > .row { display: block; width: 100%; }
.workflow-image { max-height: 280px; object-fit: contain; }
.native-workflow .actions { flex-wrap: wrap; }

/* Settings-style pages: section rail on the left */
.control-pages { display: grid; grid-template-columns: minmax(200px, 24%) minmax(0, 1fr); gap: 32px; align-items: start; }
.workflow-sidebar { position: sticky; top: 0; min-width: 0; }
.workflow-sidebar .workflow-list { max-height: calc(100vh - 220px); padding: 10px 12px 10px 10px; }
.workflow-sidebar .workflow-row { margin: 2px 0; }
.workflow-sidebar .row { min-width: 0; min-height: 52px; margin: 0; padding: 6px 16px; font-size: 17px; white-space: nowrap; border-image: none !important; border: 0; background: transparent; border-radius: var(--pc-radius); }
.workflow-sidebar .row .row-text { overflow: hidden; text-overflow: ellipsis; }
.workflow-sidebar .row:hover:not(:disabled) { background: var(--pc-surface); }
.workflow-sidebar .row[aria-pressed=true] { background: var(--pc-raised); box-shadow: none; color: var(--pc-text); }
/* Prism's page icons come through inconsistently (missing or full-colour), so the rail is text only. */
.workflow-sidebar .row img { display: none; }
.workflow-page { min-width: 0; }
.workflow-sidebar .actions:empty { display: none; }

/* Dialog button rows stay reachable at the bottom of long pages */
/* bottom offset cancels the page's bottom padding so nothing scrolls visibly beneath the bar */
.control-buttonbox { position: sticky; bottom: -32px; z-index: 2; display: flex; justify-content: flex-end; gap: 12px; margin-top: 24px; padding: 16px 0 32px; background: var(--ore-base); border-top: 1px solid var(--pc-raised); }
.control-buttonbox > .workflow-control { display: inline-block; }
@media (max-width: 900px) { .control-buttonbox { bottom: -24px; padding-bottom: 24px; } }
@media (max-width: 760px) { .control-pages { grid-template-columns: 1fr; } .workflow-sidebar { position: static; } .workflow-sidebar .workflow-list { max-height: 180px; } .setting-field { grid-template-columns: 1fr; gap: 6px; } }
</style>
