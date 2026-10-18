// Frozen OpenGL/reference implementation from f2ddbb2aa7. LGPL-2.1.
std::optional<VSWorldScene::ObjectHandle> VSWorldScene::object(const LLUUID& identity) const
{
    for (const auto& [local, value] : mImpl->objects)
        if (value.id == identity && mImpl->linksCurrent(value))
            return object(mImpl->host, local);
    for (const auto& [host, region] : mNeighbours)
        if (auto value = region->object(identity))
            return value;
    return std::nullopt;
}
