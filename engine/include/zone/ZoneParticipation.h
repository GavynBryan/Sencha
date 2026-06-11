#pragma once

struct ZoneParticipation
{
    bool Visible = false;
    bool Physics = false;
    bool Logic = false;
    bool Audio = false;

    // False means the zone is dormant: attached but invisible to every frame
    // span — it cannot affect simulation or presentation.
    [[nodiscard]] bool Any() const { return Visible || Physics || Logic || Audio; }
};
