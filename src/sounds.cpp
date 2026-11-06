#include "sounds.h"
#include "components.h"
#include "ecs.h"

namespace {
std::vector<QueuedSound> gQueuedSounds;
}

void queueSound(Sfx s) {
    for (const QueuedSound& q : gQueuedSounds) {
        if (q.sfx == s && !q.placed) return;
    }
    gQueuedSounds.push_back(QueuedSound{ s, false });
}

void queueSoundAt(Sfx s, float x, float y, float z) {
    gQueuedSounds.push_back(QueuedSound{ s, true, x, y, z });
}

void queueSoundFrom(World& w, Sfx s, Entity e) {
    if (const Transform* t = w.get<Transform>(e)) queueSoundAt(s, t->x, t->y + 0.5f, t->z);
    else queueSound(s);
}

std::vector<QueuedSound> takeQueuedSounds() {
    std::vector<QueuedSound> out;
    out.swap(gQueuedSounds);
    return out;
}

