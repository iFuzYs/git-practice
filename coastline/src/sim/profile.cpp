#include "profile.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace cl {

int Profile::addXP(int v) {
    xp += v;
    int ups = 0;
    while (xp >= xpForLevel(level)) {
        xp -= xpForLevel(level);
        level++;
        wheelspins++;
        ups++;
    }
    return ups;
}

int Profile::eventsDone() const {
    int n = 0;
    for (auto& e : eventPlace)
        if (e.second > 0) n++;
    return n;
}

int Profile::totalStars() const {
    int n = 0;
    for (auto& s : stuntStars) n += s.second;
    return n;
}

bool Profile::owns(const std::string& id) const {
    for (auto& c : garage)
        if (c.id == id) return true;
    return false;
}

std::string Profile::serialize() const {
    std::ostringstream o;
    o << "coastline 1\n";
    o << "started " << (started ? 1 : 0) << "\n";
    o << "level " << level << "\nxp " << xp << "\ncredits " << credits << "\nwheelspins " << wheelspins << "\n";
    o << "current " << current << "\nbestchain " << bestChain << "\ndistance " << distanceKm << "\ntime " << timeOfDay << "\n";
    for (auto& c : garage) o << "car " << c.id << " " << c.color << "\n";
    for (auto& e : eventPlace) o << "event " << e.first << " " << e.second << "\n";
    for (auto& s : stuntBest) o << "stunt " << s.first << " " << s.second << " " << (stuntStars.count(s.first) ? stuntStars.at(s.first) : 0) << "\n";
    for (int b : boards) o << "board " << b << "\n";
    const Settings& st = settings;
    o << "set " << st.quality << " " << st.shadows << " " << st.abs << " " << st.tcs << " " << st.stm << " " << st.autoGear << " " << st.steerAssist << " "
      << st.line << " " << st.difficulty << " " << st.master << " " << st.music << " " << st.sfx << " " << st.music_on << " " << st.camSens << " " << st.camera << " "
      << st.timeSpeed << "\n";
    return o.str();
}

bool Profile::parse(const std::string& s) {
    std::istringstream in(s);
    std::string line;
    if (!std::getline(in, line) || line.rfind("coastline", 0) != 0) return false;
    Profile p;
    p.garage.clear();
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string k;
        l >> k;
        if (k == "started") { int v; l >> v; p.started = v != 0; }
        else if (k == "level") l >> p.level;
        else if (k == "xp") l >> p.xp;
        else if (k == "credits") l >> p.credits;
        else if (k == "wheelspins") l >> p.wheelspins;
        else if (k == "current") l >> p.current;
        else if (k == "bestchain") l >> p.bestChain;
        else if (k == "distance") l >> p.distanceKm;
        else if (k == "time") l >> p.timeOfDay;
        else if (k == "car") { OwnedCar c; l >> c.id >> c.color; if (!c.id.empty()) p.garage.push_back(c); }
        else if (k == "event") { std::string id; int v; l >> id >> v; p.eventPlace[id] = v; }
        else if (k == "stunt") { std::string id; float v; int st = 0; l >> id >> v >> st; p.stuntBest[id] = v; p.stuntStars[id] = st; }
        else if (k == "board") { int b; l >> b; p.boards.push_back(b); }
        else if (k == "set") {
            Settings& st = p.settings;
            l >> st.quality >> st.shadows >> st.abs >> st.tcs >> st.stm >> st.autoGear >> st.steerAssist >> st.line >> st.difficulty >> st.master >> st.music >> st.sfx >>
                st.music_on >> st.camSens >> st.camera >> st.timeSpeed;
        }
    }
    if (p.level < 1) p.level = 1;
    if (p.current < 0 || p.current >= (int)p.garage.size()) p.current = 0;
    *this = p;
    return true;
}

}  // namespace cl
