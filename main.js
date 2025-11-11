let fireworks = []
let particles = []

const EventType = {
    QUIT: 0x100,
    KeyDown: 0x300,
    KeyUp: 0x301,
    MouseMotion: 0x400,
    MouseButtonDown: 0x401,
    MouseButtonUp: 0x402,
}

function hslToRgb(h, s, l) {
    // Accept h in degrees, s and l as 0..1
    h = ((h % 360) + 360) % 360;
    if (s > 1) s /= 100;
    if (l > 1) l /= 100;

    const c = (1 - Math.abs(2 * l - 1)) * s;
    const hh = h / 60;
    const x = c * (1 - Math.abs((hh % 2) - 1));

    let r1 = 0, g1 = 0, b1 = 0;
    if (hh >= 0 && hh < 1) { r1 = c; g1 = x; b1 = 0; }
    else if (hh < 2) { r1 = x; g1 = c; b1 = 0; }
    else if (hh < 3) { r1 = 0; g1 = c; b1 = x; }
    else if (hh < 4) { r1 = 0; g1 = x; b1 = c; }
    else if (hh < 5) { r1 = x; g1 = 0; b1 = c; }
    else { r1 = c; g1 = 0; b1 = x; }

    const m = l - c / 2;
    const r = Math.round((r1 + m) * 255);
    const g = Math.round((g1 + m) * 255);
    const b = Math.round((b1 + m) * 255);

    return [r, g, b];
}

class Firework {
    constructor(canvas, x, y, targetX, targetY) {
        this.canvas = canvas
        this.x = x
        this.y = y
        this.targetX = targetX
        this.targetY = targetY
        this.speed = 3
        this.angle = Math.atan2(targetY - y, targetX - x)
        this.vx = Math.cos(this.angle) * this.speed
        this.vy = Math.sin(this.angle) * this.speed
        this.distanceToTarget = Math.hypot(targetX - x, targetY - y)
        this.distanceTraveled = 0
        this.dead = false
    }

    explode() {
        for (let i = 0; i < 60; i++) {
            particles.push(new Particle(this.canvas, this.x, this.y))
        }
    }

    update() {
        this.x += this.vx
        this.y += this.vy
        this.distanceTraveled += this.speed
        if (this.distanceTraveled >= this.distanceToTarget) {
            this.dead = true
            this.explode()
        }
    }

    draw() {
        const rgb = hslToRgb(Math.floor(Math.random() * 360), 1, Math.random())
        const canvas = this.canvas
        canvas.beginPath()
        canvas.arc(this.x, this.y, 2, 0, Math.PI * 2)
        canvas.setFillColor(rgb[0], rgb[1], rgb[2])
        canvas.fill()
    }
}

class Particle {
    constructor(canvas, x, y) {
        this.canvas = canvas
        this.x = x
        this.y = y
        this.speed = Math.random() * 4 + 1
        this.angle = Math.random() * Math.PI * 2
        this.vx = Math.cos(this.angle) * this.speed
        this.vy = Math.sin(this.angle) * this.speed
        this.gravity = 0.05

        this.alpha = 1
        this.decay = Math.random() * 0.015 + 0.003
        this.dead = false
        this.rgb = [
            Math.floor(Math.random() * 255),
            Math.floor(Math.random() * 255),
            Math.floor(Math.random() * 255),
        ]
    }

    update() {
        this.vx *= 0.98
        this.vy *= 0.98
        this.vy += this.gravity
        this.x += this.vx
        this.y += this.vy
        this.alpha -= this.decay
        if (this.alpha <= 0) {
            this.dead = true
        }
    }

    draw() {
        const canvas = this.canvas
        canvas.setGlobalAlpha(this.alpha)
        canvas.beginPath()
        canvas.arc(this.x, this.y, 2, 0, Math.PI * 2)
        canvas.setFillColor(this.rgb[0], this.rgb[1], this.rgb[2])
        canvas.fill()
        canvas.setGlobalAlpha(1)  // 重置全局透明度
    }
}

function draw(canvas) {
    // 覆盖一层半透明的黑色，制造拖尾效果
    canvas.setFillColor(0, 0, 0, 100)
    canvas.fillRect(0, 0, canvas.width, canvas.height)

    let aliveFireworks = []
    for (let firework of fireworks) {
        firework.update()
        firework.draw()
        if (!firework.dead) {
            aliveFireworks.push(firework)
        }
    }
    fireworks = aliveFireworks

    let aliveParticles = []
    for (let particle of particles) {
        particle.update()
        particle.draw()
        if (!particle.dead) {
            aliveParticles.push(particle)
        }
    }
    particles = aliveParticles
}

function get_events(canvas) {
    let events = []
    while (true) {
        let event = canvas.pollEvent()
        if (!event) break
        events.push(event)
    }
    return events
}

function main() {
    const canvas = new Canvas(800, 600)

    while (true) {
        let events = get_events(canvas)
        for (let event of events) {
            if (event.type === EventType.QUIT) {
                canvas.quit()
                return
            }
            else if (event.type === EventType.MouseButtonDown) {
                let x = event.x
                let y = event.y
                let startX = canvas.width / 2
                let startY = canvas.height
                fireworks.push(new Firework(canvas, startX, startY, x, y))
            }
        }
        draw(canvas)
        canvas.show()
        os.sleep(1000 / 60)  // 60 FPS
    }
}

main()
