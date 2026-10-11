#include "GameApplication.hpp"

namespace rts::game {
void GameApplication::processWorldEvents() {
    for (const auto& event : game_.takeEvents()) {
        if (const auto* harvest = std::get_if<CrystalsHarvested>(&event)) {
            FloatingNotification notification;
            notification.position = harvest->position;
            notification.height = harvest->height;
            notification.text = L"+" + std::to_wstring(harvest->amount);
            notification.resourceIcon = "crystal";
            notification.color = 0xffa044;
            notification.recipient = harvest->owner;
            notification.started = harvest->tick;
            ui_.floatingNotifications.publish(std::move(notification), game_.player().id, game_.fog());
        }
    }
    ui_.floatingNotifications.expire(game_.clock().elapsedTicks());
}
}
