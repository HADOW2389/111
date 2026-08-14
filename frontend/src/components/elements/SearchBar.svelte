<script lang="ts">
    import { translations, translate, isJarvisRunning, ipcConnected, sendTextCommand, lastAssistantResponse } from "@/stores"
    
    $: t = (key: string) => translate($translations, key)
    
    let searchQuery = ""
    let isProcessing = false
    let statusMessage = ""

    async function handleSubmit(e: Event) {
        e.preventDefault()
        
        const command = searchQuery.trim()
        if (!command || isProcessing) return
        
        if (!$isJarvisRunning || !$ipcConnected) {
            statusMessage = t('search-error-not-running')
            setTimeout(() => statusMessage = "", 3000)
            return
        }

        isProcessing = true
        statusMessage = ""

        try {
            await sendTextCommand(command)
            searchQuery = ""
        } catch (err) {
            console.error("Failed to send command:", err)
            statusMessage = t('search-error-failed')
            setTimeout(() => statusMessage = "", 3000)
        } finally {
            isProcessing = false
        }
    }

    function handleKeydown(e: KeyboardEvent) {
        if (e.key === "Escape") {
            searchQuery = ""
        }
    }
</script>

<div id="search-form" class="search" class:active={searchQuery !== ""} class:processing={isProcessing}>
    <form on:submit={handleSubmit}>
        <input
            bind:value={searchQuery}
            on:keydown={handleKeydown}
            type="text"
            name="q"
            placeholder={t('search-placeholder')}
            autocomplete="off"
            minlength="1"
            maxlength="200"
            disabled={isProcessing}
        />
        <small>{isProcessing ? '...' : 'Enter'}</small>
    </form>
    {#if statusMessage}
        <div class="search-status">{statusMessage}</div>
    {/if}
</div>

{#if $lastAssistantResponse}
    <div class="assistant-response">{$lastAssistantResponse}</div>
{/if}

<style lang="scss">
    .search.processing input {
        opacity: 0.6;
        cursor: wait;
    }

    .search-status {
        position: absolute;
        bottom: -24px;
        left: 50%;
        transform: translateX(-50%);
        font-size: 0.75rem;
        color: rgba(82, 254, 254, 0.8);
        white-space: nowrap;
        animation: fadeIn 0.2s ease;
    }

    .assistant-response {
        margin: 1rem auto 0;
        max-width: 760px;
        padding: 0.85rem 1rem;
        border: 1px solid rgba(82, 254, 254, 0.25);
        border-radius: 8px;
        color: rgba(255, 255, 255, 0.9);
        background: rgba(8, 20, 28, 0.72);
        white-space: pre-wrap;
        line-height: 1.45;
    }

    @keyframes fadeIn {
        from { opacity: 0; transform: translateX(-50%) translateY(-5px); }
        to { opacity: 1; transform: translateX(-50%) translateY(0); }
    }
</style>
