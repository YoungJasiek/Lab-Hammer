-- ==========================================================
-- Frozen-Life: Gameplay Mechanics Script (Lua 5.4)
-- Lab Engine :: Moddable Gameplay Architecture
-- ==========================================================

-- Player combat attributes & survival rules
PlayerRules = {
    maxHealth = 150.0,
    startSuitArmor = 50.0,
    armorAbsorptionRatio = 0.70,     -- Kevlar suit absorbs 70% of kinetic/blast damage
    respawnTime = 4.0,              -- Seconds until respawn is allowed
    barrelDamageMultiplier = 0.75   -- Blast attenuation multiplier for explosive barrels
}

-- Environmental Biometric Retinal Scanner mechanics
RetinalScanner = {
    scanDuration = 1.25,            -- Seconds required to complete retinal scan
    authorizedUser = "DR. VANCE",   -- Authenticated cryogenic facility researcher
    clearanceLevel = 3,             -- Level 3 Security clearance (Airlock chambers)
    targetDoorIndex = 0             -- Linked airlock blast door
}

-- Pickups & Resource quantities
Pickups = {
    MedkitHeal = 50.0,              -- Health recovered by tactical medkit
    AmmoBoxAmount = 36,             -- Cartridges per standard ammo box
    WeaponPadRespawnTime = 60.0     -- Arena weapon spawn cooldown in seconds
}

-- Complete 9-weapon arsenal balancing table
Weapons = {
    Pipe = {
        id = 0,
        name = "RURKA METALOWA",
        damage = 55.0,
        range = 2.6,
        fireRate = 0.55,
        isMelee = true,
        slot = 1,
        recoilPitch = 0.04,
        recoilKick = 0.05
    },
    Pistol = {
        id = 1,
        name = "PISTOLET 9MM",
        damage = 24.0,
        fireRate = 0.22,
        clipSize = 18,
        reserve = 144,
        slot = 2,
        recoilPitch = 0.035,
        recoilKick = 0.045
    },
    Shotgun = {
        id = 2,
        name = "STRZELBA SPAS-12",
        damage = 14.0,
        fireRate = 0.85,
        clipSize = 8,
        reserve = 64,
        slot = 3,
        recoilPitch = 0.09,
        recoilKick = 0.12
    },
    M4A4S = {
        id = 3,
        name = "KARABIN M4A4-S",
        damage = 28.0,
        fireRate = 0.11,
        clipSize = 30,
        reserve = 180,
        isAuto = true,
        slot = 4,
        recoilPitch = 0.038,
        recoilKick = 0.05
    },
    SG553 = {
        id = 4,
        name = "KARABIN SG553",
        damage = 35.0,
        fireRate = 0.14,
        clipSize = 30,
        reserve = 150,
        isAuto = true,
        slot = 5,
        recoilPitch = 0.045,
        recoilKick = 0.06
    },
    Minigun = {
        id = 5,
        name = "MINIGUN VULCAN",
        damage = 22.0,
        fireRate = 0.065,
        clipSize = 150,
        reserve = 450,
        isAuto = true,
        slot = 6,
        recoilPitch = 0.022,
        recoilKick = 0.035
    },
    PlasmaGun = {
        id = 6,
        name = "PLAZMA GUN",
        damage = 45.0,
        splashDamage = 25.0,
        splashRadius = 3.5,
        fireRate = 0.28,
        clipSize = 25,
        reserve = 100,
        isProj = true,
        slot = 7,
        recoilPitch = 0.035,
        recoilKick = 0.05
    },
    Railgun = {
        id = 7,
        name = "RAILGUN",
        damage = 135.0,
        fireRate = 1.4,
        clipSize = 5,
        reserve = 25,
        slot = 8,
        recoilPitch = 0.12,
        recoilKick = 0.18
    },
    RPG = {
        id = 8,
        name = "WYRZUTNIA RPG",
        damage = 120.0,
        splashDamage = 75.0,
        splashRadius = 5.5,
        fireRate = 1.2,
        clipSize = 1,
        reserve = 12,
        isProj = true,
        slot = 9,
        recoilPitch = 0.14,
        recoilKick = 0.22
    }
}

-- ==========================================================
-- Game Logic & Event Callbacks (Executed by C++ Engine)
-- ==========================================================

-- Pure Lua damage calculation function
function CalculateDamage(incomingDamage, currentArmor, currentHealth)
    local absorbRatio = PlayerRules.armorAbsorptionRatio or 0.70
    local absorbed = 0.0
    if currentArmor > 0.0 then
        absorbed = math.min(currentArmor, incomingDamage * absorbRatio)
    end
    local finalDmg = incomingDamage - absorbed
    local isLethal = (currentHealth - finalDmg <= 0.0)
    return finalDmg, absorbed, isLethal
end

-- Event triggered when retinal scan completes successfully
function OnRetinalScanComplete(scannerId, linkedDoorIndex)
    Lab.log("Retinal scan verified for " .. RetinalScanner.authorizedUser .. " on door " .. tostring(linkedDoorIndex))
    Lab.unlockDoor(linkedDoorIndex)
    Lab.playSound(25) -- SoundID::AccessGranted
    Lab.addChatMessage("[SECURITY]", "Retinal scan verified: Door " .. tostring(linkedDoorIndex) .. " unlocked for " .. RetinalScanner.authorizedUser)
    return true
end
