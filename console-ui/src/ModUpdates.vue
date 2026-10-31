<script setup lang="ts">
import { t } from './i18n'
// Mod updates inside Mods & packs: check Modrinth, review what changes, update all or a chosen few.
import { computed, ref, watch } from 'vue'
import { invoke, type ModUpdateState } from './bridge'
const props = defineProps<{ instanceId: string; state: ModUpdateState; busy: boolean; running: boolean }>()
const emit = defineEmits<{ error: [message: string] }>()
const skipped = ref(new Set<string>())
const current = computed(() => props.state.instance === props.instanceId ? props.state : {})
const updates = computed(() => current.value.updates || [])
const chosen = computed(() => updates.value.filter(u => !skipped.value.has(u.name)).map(u => u.name))
watch(() => current.value.updates, () => { skipped.value = new Set() })
function toggle(name: string) {
  const next = new Set(skipped.value)
  next.has(name) ? next.delete(name) : next.add(name)
  skipped.value = next
}
function call(method: string, args: Record<string, unknown>) { invoke(method, { id: props.instanceId, ...args }).catch(e => emit('error', String(e))) }
</script>

<template>
  <section class="mod-updates" aria-labelledby="mod-updates-title">
    <h3 id="mod-updates-title" class="section-title">{{ t('updates.updates') }}</h3>
    <p v-if="current.managed" class="muted">{{ t('updates.this_instance_was_installed_from') }}</p>
    <template v-if="current.phase === 'checking'"><p role="status">{{ t('updates.checking_modrinth_for_newer_releases') }}</p></template>
    <template v-else-if="current.phase === 'failed'"><p role="alert" class="workflow-error">{{ current.error }}</p></template>
    <template v-else-if="current.phase === 'ready'">
      <p v-if="current.scan?.pinned" class="muted">{{ current.scan.pinned === 1 ? t('updates.pinnedOne') : t('updates.pinnedMany', { count: current.scan.pinned }) }}</p>
      <p v-if="!current.total && !current.scan?.pinned" role="status">{{ t('updates.this_instance_has_no_mods') }}</p>
      <p v-else-if="current.total" role="status">{{ updates.length === 1 ? t('updates.availableOne') : updates.length ? t('updates.availableMany', { count: updates.length }) : t('updates.upToDate') }}
        <span class="muted"> {{ t('updates.checked', { checked: current.checked || 0, total: current.total || 0 }) }}<template v-if="(current.total || 0) > (current.checked || 0)">{{ t('updates.restNotOnModrinth') }}</template>.</span></p>
      <div class="resource-list">
        <button v-for="u in updates" :key="u.name" class="row setting-toggle" role="switch" :aria-checked="!skipped.has(u.name)" @click="toggle(u.name)">
          <span class="setting-text"><span>{{ u.name }}<span v-if="u.beta" class="bp-chip chip-quiet update-beta">{{ t('updates.beta') }}</span></span><small>{{ u.from }} → {{ u.to }}<template v-if="u.changelog"> · {{ u.changelog }}</template></small></span>
          <span class="switch" aria-hidden="true"><span /></span>
        </button>
      </div>
    </template>
    <div class="actions">
      <button v-if="current.phase === 'ready' && chosen.length" class="primary" :disabled="busy || running" data-action="apply-updates" @click="call('applyModUpdates', { names: chosen })">{{ chosen.length === updates.length && chosen.length > 1 ? t('updates.updateAll') : chosen.length === 1 ? t('updates.updateOne') : t('updates.updateSome', { count: chosen.length }) }}</button>
      <button :disabled="busy || running || current.phase === 'checking'" data-action="check-updates" @click="call('checkModUpdates', {})">{{ current.phase ? t('updates.checkAgain') : t('updates.check') }}</button>
    </div>
    <p v-if="running" class="muted">{{ t('updates.stop_the_game_to_update') }}</p>
  </section>
</template>

<style>
.mod-updates { margin-top: 8px; }
.update-beta { margin-left: 10px; font-size: 14px; padding: 2px 10px; border-radius: 999px; }
</style>
