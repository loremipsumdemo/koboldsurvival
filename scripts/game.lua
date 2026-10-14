math.randomseed(os.time())

local GRID = 16
local MOVE_TIME = 0.1

local state = {
    playerX = 0, playerY = 0,
    keyX = 0, keyY = 0,
    doorX = 0, doorY = 0,
    keyExists = true,
}

local visualX, visualZ = 0, 0
local segStartX, segStartZ = 0, 0
local segEndX, segEndZ = 0, 0
local segT = 1.0
local moveAccum = 0.0
local path = {}

local function tileCenter(gx, gy)
    return gx + 0.5, gy + 0.5
end

local function generate()
    local px, py, kx, ky, dx, dy
    repeat
        px = math.random(0, GRID - 1); py = math.random(0, GRID - 1)
        kx = math.random(0, GRID - 1); ky = math.random(0, GRID - 1)
        dx = math.random(0, GRID - 1); dy = math.random(0, GRID - 1)
    until not (px == kx and py == ky)
      and not (px == dx and py == dy)
      and not (kx == dx and ky == dy)

    state.playerX, state.playerY = px, py
    state.keyX, state.keyY = kx, ky
    state.doorX, state.doorY = dx, dy
    state.keyExists = true

    visualX, visualZ = tileCenter(px, py)
    segStartX, segStartZ = visualX, visualZ
    segEndX, segEndZ = visualX, visualZ
    segT = 1.0
    moveAccum = 0.0
    path = {}
end

local function checkKeyPickup()
    if state.keyExists
       and state.playerX == state.keyX
       and state.playerY == state.keyY then
        state.keyExists = false
    end
end

local function atDoor()
    return not state.keyExists
       and state.playerX == state.doorX
       and state.playerY == state.doorY
       and segT >= 1.0
end

local function heuristic(ax, ay, bx, by)
    local dx = math.abs(ax - bx)
    local dy = math.abs(ay - by)
    return math.max(dx, dy)
end

local function findPath(sx, sy, tx, ty)
    if sx == tx and sy == ty then return {} end
    if tx < 0 or tx >= GRID or ty < 0 or ty >= GRID then return {} end

    local function key(x, y) return y * GRID + x end

    local startK = key(sx, sy)
    local goalK  = key(tx, ty)

    local gScore = { [startK] = 0 }
    local fScore = { [startK] = heuristic(sx, sy, tx, ty) }
    local cameFrom = {}
    local open = { { x = sx, y = sy, k = startK } }
    local inOpen = { [startK] = true }
    local closed = {}

    local neighbors = {
        {  0, -1 }, {  0,  1 }, { -1,  0 }, {  1,  0 },
        { -1, -1 }, {  1, -1 }, { -1,  1 }, {  1,  1 },
    }

    while #open > 0 do
        local bestIdx = 1
        local bestF = fScore[open[1].k] or math.huge
        for i = 2, #open do
            local f = fScore[open[i].k] or math.huge
            if f < bestF then
                bestF = f
                bestIdx = i
            end
        end

        local current = table.remove(open, bestIdx)
        inOpen[current.k] = nil

        if current.k == goalK then
            local result = {}
            local k = current.k
            while k ~= startK do
                local cx = k % GRID
                local cy = math.floor(k / GRID)
                table.insert(result, 1, { x = cx, y = cy })
                k = cameFrom[k]
            end
            return result
        end

        closed[current.k] = true

        for _, n in ipairs(neighbors) do
            local nx = current.x + n[1]
            local ny = current.y + n[2]
            if nx >= 0 and nx < GRID and ny >= 0 and ny < GRID then
                local nk = key(nx, ny)
                if not closed[nk] then
                    local tentativeG = (gScore[current.k] or math.huge) + 1
                    if tentativeG < (gScore[nk] or math.huge) then
                        cameFrom[nk] = current.k
                        gScore[nk] = tentativeG
                        fScore[nk] = tentativeG + heuristic(nx, ny, tx, ty)
                        if not inOpen[nk] then
                            table.insert(open, { x = nx, y = ny, k = nk })
                            inOpen[nk] = true
                        end
                    end
                end
            end
        end
    end

    return {}
end

local function beginSegment(newGX, newGY)
    segStartX, segStartZ = visualX, visualZ
    segEndX, segEndZ = tileCenter(newGX, newGY)
    segT = 0.0
end

function step(action, a1, a2)
    if action == "moveTo" then
        path = findPath(state.playerX, state.playerY, a1, a2)
        moveAccum = 0.0
    elseif action == "tick" then
        local dt = a1

        if segT < 1.0 then
            segT = segT + dt / MOVE_TIME
            if segT > 1.0 then segT = 1.0 end
            visualX = segStartX + (segEndX - segStartX) * segT
            visualZ = segStartZ + (segEndZ - segStartZ) * segT
        end

        if #path > 0 then
            moveAccum = moveAccum + dt

            if moveAccum >= MOVE_TIME then
                moveAccum = 0.0

                local nextStep = table.remove(path, 1)
                local dx = nextStep.x - state.playerX
                local dy = nextStep.y - state.playerY
                local adx = math.abs(dx)
                local ady = math.abs(dy)

                if (adx == 1 and ady == 0) or (adx == 0 and ady == 1) or (adx == 1 and ady == 1) then
                    state.playerX = nextStep.x
                    state.playerY = nextStep.y

                    if segT >= 1.0 then
                        beginSegment(nextStep.x, nextStep.y)
                    else
                        segStartX, segStartZ = visualX, visualZ
                        segEndX, segEndZ = tileCenter(nextStep.x, nextStep.y)
                        segT = 0.0
                    end

                    checkKeyPickup()
                else
                    path = {}
                end
            end
        else
            moveAccum = 0.0
        end

        if atDoor() then
            generate()
        end
    end
end

function getState()
    return state.playerX, state.playerY,
           state.keyX, state.keyY,
           state.doorX, state.doorY,
           state.keyExists,
           visualX, visualZ
end

generate()