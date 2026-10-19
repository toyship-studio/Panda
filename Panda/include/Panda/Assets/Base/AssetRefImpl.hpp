#pragma once

// Definitions of the AssetRef template (declarations — AssetRef.hpp). Instantiations for the
// built-in asset kinds — AssetRef.cpp; a plugin module instantiates its own kinds itself:
//   #include "Panda/Assets/Base/AssetRefImpl.hpp"
//   template class Panda::AssetRef<MyAsset>;
#include "Panda/Assets/Base/AssetHandler.hpp"
#include "Panda/GameLogic/GameContext.hpp"

namespace Panda {

namespace AssetRefDetail {

    // The counter is touched only while the handler is alive: static refs (StaticResources)
    // die after it — a decrement through a pointer into a dead map would be UB.
    inline bool handlerAlive() {
        return GameContext::getAssetHandler() != nullptr;
    }

} // namespace AssetRefDetail
using AssetRefDetail::handlerAlive;

template<typename AssetType>
AssetRef<AssetType>::~AssetRef() {
    if (m_refCounter && handlerAlive()) { --(*m_refCounter); }
}

template<typename AssetType>
AssetRef<AssetType>::AssetRef()
    : m_id(0) {}

template<typename AssetType>
AssetRef<AssetType>::AssetRef(const AssetRef<AssetType> &other)
    : m_id(other.m_id)
    , m_refCounter(other.m_refCounter)
    , m_cachedAsset(other.m_cachedAsset)
    , m_cachedGeneration(other.m_cachedGeneration) {
    if (m_refCounter && handlerAlive()) { ++(*m_refCounter); }
}

template<typename AssetType>
AssetRef<AssetType>::AssetRef(AssetRef<AssetType> &&other)
    : m_id(other.m_id)
    , m_refCounter(other.m_refCounter)
    , m_cachedAsset(other.m_cachedAsset)
    , m_cachedGeneration(other.m_cachedGeneration) {
    other.m_id = 0;
    other.m_refCounter = nullptr;
    other.m_cachedAsset = nullptr;
    other.m_cachedGeneration = 0;
}

template<typename AssetType>
AssetRef<AssetType>::AssetRef(AssetId id)
    : m_id(id) {
    AssetHandler *handler = GameContext::getAssetHandler();
    if (handler && m_id) { m_refCounter = handler->acquireRefCounter(m_id); }
}

template<typename AssetType>
AssetType *AssetRef<AssetType>::get() const {
    AssetHandler *handler = GameContext::getAssetHandler();
    if (!handler || !m_id) { return nullptr; }
    if (m_cachedAsset && m_cachedGeneration == handler->resolveGeneration()) {
        return m_cachedAsset;
    }
    // Full resolve: the type is checked here once per generation — cache hits need neither a
    // cast nor map lookups. "Not found" is not cached: the appearance of an asset
    // (registration, loading) is picked up by the very next call.
    Asset *asset = handler->hasAsset(m_id) ? handler->loadInternal(m_id) : nullptr;
    m_cachedAsset = dynamic_cast<AssetType *>(asset);
    m_cachedGeneration = handler->resolveGeneration();
    return m_cachedAsset;
}

template<typename AssetType>
void AssetRef<AssetType>::reset() {
    if (m_refCounter && handlerAlive()) { --(*m_refCounter); }
    m_id = 0;
    m_refCounter = nullptr;
    m_cachedAsset = nullptr;
    m_cachedGeneration = 0;
}

template<typename AssetType>
AssetRef<AssetType> &AssetRef<AssetType>::operator=(const AssetRef<AssetType> &other) {
    if (this == &other) { return *this; }
    if (m_refCounter && handlerAlive()) { --(*m_refCounter); }
    m_id = other.m_id;
    m_refCounter = other.m_refCounter;
    m_cachedAsset = other.m_cachedAsset;
    m_cachedGeneration = other.m_cachedGeneration;
    if (m_refCounter && handlerAlive()) { ++(*m_refCounter); }
    return *this;
}

} // namespace Panda
