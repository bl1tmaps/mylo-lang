# Building Pong in Mylo: A Beginner-Friendly Tutorial

Welcome to Mylo! Mylo is a lightweight, high-performance programming language designed for fast execution, clean syntax, and seamless C interoperability. Unlike languages that rely on unpredictable garbage collectors (which can cause micro-stutters and frame drops in real-time games), Mylo uses **Generational Memory Arenas** and **Strict Static Typing** to give you deterministic performance and rock-solid safety.

In this tutorial, you will learn the core features of Mylo by building a complete, playable 2-player **Pong** game from scratch using Mylo's built-in 2D windowing module, `PixelWindow`.

---

## What We Will Cover
- **Strict Typing & Variables**: Declaring numbers, booleans, and arrays with explicit types.
- **Control Flow**: Using `forever` loops, `if` / `else` conditionals, and `break`.
- **Object-Oriented Structs**: Defining structs with data fields and methods.
- **Module Imports**: Loading native modules with `import native`.
- **2D Graphics & Input**: Creating windows, polling keyboard events, drawing shapes, and rendering text.
- **Generational Arenas (`region`)**: Managing per-frame memory without garbage collection pauses.

---

## Prerequisites & Building Mylo

Before writing code, make sure you have built Mylo with module support enabled:

```bash
# Clone and build Mylo with CMake
git clone https://github.com/bl1tmaps/mylo-lang.git
cd mylo-lang
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMODULES=ON
cmake --build build -j8
```

Verify your build runs:
```bash
./build/mylo --version
```

---

## Step 1: Language Basics & Strict Typing

Every variable declaration in Mylo requires an explicit type annotation and an initial value:

```javascript
// Integer types: i8, i16, i32, i64, u8, u16, u32, u64
score: i32 = 0

// Floating point types: f32, f64
ball_speed: f64 = 5.5

// Booleans and Strings
game_over: bool = false
title: string = "Mylo Pong"

print(f"{title} - Player Score: {score}")
```

Mylo strictly prevents common runtime errors at compile time:
- Undeclared variables cannot be used (`x = 5` is an error: what is `x`?).
- Variables cannot change types (`score = "high"` is rejected).
- Variables cannot be redefined in the same scope.

---

## Step 2: Defining Game Entities with Structs

In Pong, we have two primary game elements: **Paddles** and a **Ball**. Mylo lets you define custom types using the `struct` keyword, complete with fields and object methods.

### The Paddle Struct
A paddle has an $(x, y)$ position, dimensions (width and height), a vertical movement speed, and methods to move up and down within screen boundaries:

```javascript
struct Paddle {
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    speed: i32,

    fn move_up(self) {
        self.y = self.y - self.speed
        if (self.y < 10) {
            self.y = 10
        }
    }

    fn move_down(self, max_y: i32) {
        self.y = self.y + self.speed
        if (self.y + self.h > max_y - 10) {
            self.y = max_y - 10 - self.h
        }
    }
}
```

### The Ball Struct
The ball has a position, horizontal and vertical velocity (`vx`, `vy`), a radius, and a reset method when a point is scored:

```javascript
struct Ball {
    x: i32,
    y: i32,
    vx: i32,
    vy: i32,
    radius: i32,

    fn reset(self, start_x: i32, start_y: i32, dir_x: i32) {
        self.x = start_x
        self.y = start_y
        self.vx = dir_x
        self.vy = 3
    }
}
```

### Initializing Struct Instances
You instantiate structs with brace literals:

```javascript
p1: Paddle = {x=30, y=200, w=15, h=80, speed=6}
p2: Paddle = {x=595, y=200, w=15, h=80, speed=6}
ball: Ball = {x=320, y=240, vx=4, vy=3, radius=6}
```

---

## Step 3: Setting Up PixelWindow & The Game Canvas

Mylo includes a native graphics module called `PixelWindow` (`PW`), providing hardware-accelerated presentation across macOS (Metal), Windows (GDI), Linux (X11), and AmigaOS!

To load the module, inform Mylo of the search path and import it natively:

```javascript
module_path("modules/pixel_window/")
import native "pixel_window.mylo"

// Window dimensions
screen_w: i32 = 640
screen_h: i32 = 480

// Create window and pixel buffer
win: i32 = PW::create_window(screen_w, screen_h, "Mylo Pong", false)
buffer: u8[] = PW::create_buffer(screen_w, screen_h)
```

The pixel buffer is a tightly packed `u8[]` array representing RGB color pixels $(640 \times 480 \times 3 \text{ bytes})$.

---

## Step 4: The Main Loop & Keyboard Input

Games run inside a continuous loop that executes once per frame. In Mylo, use `forever { ... }`:

```javascript
forever {
    // 1. Process OS window events; exit if the user closes the window
    if (PW::process_events(win) == false) {
        break
    }

    // 2. Allow quitting immediately with the ESCAPE key
    if (PW::is_key_pressed(win, PW::KEY_ESCAPE)) {
        break
    }

    // 3. Player 1 Controls (W and S keys)
    if (PW::is_key_down(win, PW::KEY_W)) {
        p1.move_up()
    }
    if (PW::is_key_down(win, PW::KEY_S)) {
        p1.move_down(screen_h)
    }

    // 4. Player 2 Controls (UP and DOWN arrow keys)
    if (PW::is_key_down(win, PW::KEY_UP)) {
        p2.move_up()
    }
    if (PW::is_key_down(win, PW::KEY_DOWN)) {
        p2.move_down(screen_h)
    }

    // ... game logic and drawing will go here ...
}

// Clean up OS window resources on exit
PW::destroy_window(win)
```

---

## Step 5: Ball Physics & Wall Collisions

Each frame, the ball advances by its velocity:

```javascript
ball.x = ball.x + ball.vx
ball.y = ball.y + ball.vy
```

### Bouncing Off Top & Bottom Boundaries
If the ball hits the top or bottom wall, reverse its vertical velocity:

```javascript
// Top wall collision
if (ball.y - ball.radius <= 10) {
    ball.y = 10 + ball.radius
    ball.vy = 0 - ball.vy
}

// Bottom wall collision
if (ball.y + ball.radius >= screen_h - 10) {
    ball.y = screen_h - 10 - ball.radius
    ball.vy = 0 - ball.vy
}
```

### Scoring Points
When the ball passes the left or right edges, a player scores and the ball resets toward the scorer:

```javascript
// Player 2 scores (ball passed left side)
if (ball.x < 0) {
    score_p2 = score_p2 + 1
    ball.reset(screen_w / 2, screen_h / 2, 4)
}

// Player 1 scores (ball passed right side)
if (ball.x > screen_w) {
    score_p1 = score_p1 + 1
    ball.reset(screen_w / 2, screen_h / 2, -4)
}
```

---

## Step 6: Paddle Collisions (Bounding Box Intersection)

To check if the ball hits a paddle, test whether the ball's bounding box intersects the paddle rectangle. If it does, invert horizontal velocity `ball.vx`:

```javascript
// Collision with Player 1 (Left Paddle)
if (ball.vx < 0) {
    if (ball.x - ball.radius <= p1.x + p1.w) {
        if (ball.x + ball.radius >= p1.x) {
            if (ball.y >= p1.y) {
                if (ball.y <= p1.y + p1.h) {
                    ball.vx = 0 - ball.vx
                    ball.x = p1.x + p1.w + ball.radius
                }
            }
        }
    }
}

// Collision with Player 2 (Right Paddle)
if (ball.vx > 0) {
    if (ball.x + ball.radius >= p2.x) {
        if (ball.x - ball.radius <= p2.x + p2.w) {
            if (ball.y >= p2.y) {
                if (ball.y <= p2.y + p2.h) {
                    ball.vx = 0 - ball.vx
                    ball.x = p2.x - ball.radius
                }
            }
        }
    }
}
```

---

## Step 7: Zero-Overhead Memory with `region`

In game development, allocating temporary variables in every frame (e.g. vectors, particle trails, formatted strings) can quickly trigger garbage collector stalls.

Mylo solves this with **Lexical Scoped Arenas** (`region`). Everything allocated inside a `region` block is placed in a bump-allocated memory chunk. When the block exits, that entire block is instantly reset in $O(1)$ time with zero cleanup overhead!

Here we use a `region` to compute a trailing motion blur effect behind the ball:

```javascript
region {
    // Temporary trail calculations allocated in this frame's arena
    trail_x: i32[] = [ball.x - ball.vx, ball.x - (ball.vx * 2)]
    trail_y: i32[] = [ball.y - ball.vy, ball.y - (ball.vy * 2)]

    // Draw faint trailing circles
    PW::draw_circle(buffer, screen_w, screen_h, trail_x[0], trail_y[0], ball.radius - 1, 1, true, 200, 180, 50)
    PW::draw_circle(buffer, screen_w, screen_h, trail_x[1], trail_y[1], ball.radius - 3, 1, true, 120, 100, 30)
}
// Arena memory is instantly reclaimed here!
```

---

## Step 8: Drawing the Scene & Updating the Display

Finally, render all visuals each frame:
1. **Clear Screen**: `PW::clear_buffer(buffer, w, h, r, g, b)`
2. **Center Net**: Draw a dashed dividing line with `PW::draw_line` or `PW::draw_rect`.
3. **Paddles & Ball**: Draw filled rectangles and circles.
4. **Scores**: Render text using `PW::draw_text(buffer, w, h, text, x, y, size, r, g, b)`.
5. **Display**: Present the frame to the OS window via `PW::update_window(win, buffer)`.

---

## Complete, Runnable Game (`pong.mylo`)

Save the following code as `pong.mylo` in the root of your Mylo repository:

```javascript
// =============================================================================
// pong.mylo - A Complete 2-Player Pong Game in Mylo
// =============================================================================

module_path("modules/pixel_window/")
import native "pixel_window.mylo"

// --- Screen Settings ---
screen_w: i32 = 640
screen_h: i32 = 480

// --- Entity Definitions ---
struct Paddle {
    x: i32,
    y: i32,
    w: i32,
    h: i32,
    speed: i32,

    fn move_up(self) {
        self.y = self.y - self.speed
        if (self.y < 12) {
            self.y = 12
        }
    }

    fn move_down(self, max_y: i32) {
        self.y = self.y + self.speed
        if (self.y + self.h > max_y - 12) {
            self.y = max_y - 12 - self.h
        }
    }
}

struct Ball {
    x: i32,
    y: i32,
    vx: i32,
    vy: i32,
    radius: i32,

    fn reset(self, start_x: i32, start_y: i32, dir_x: i32) {
        self.x = start_x
        self.y = start_y
        self.vx = dir_x
        self.vy = 3
    }
}

// --- Initialize Game Objects ---
p1: Paddle = {x=25, y=200, w=14, h=80, speed=6}
p2: Paddle = {x=screen_w - 39, y=200, w=14, h=80, speed=6}
ball: Ball = {x=screen_w / 2, y=screen_h / 2, vx=4, vy=3, radius=6}

score_p1: i32 = 0
score_p2: i32 = 0

// --- Create Window and Frame Buffer ---
win: i32 = PW::create_window(screen_w, screen_h, "Mylo Pong", false)
buffer: u8[] = PW::create_buffer(screen_w, screen_h)

// --- Game Loop ---
forever {
    // 1. Process OS window events
    if (PW::process_events(win) == false) {
        break
    }

    // 2. Exit on ESCAPE key
    if (PW::is_key_pressed(win, PW::KEY_ESCAPE)) {
        break
    }

    // 3. User Input
    // Player 1: W (Up) / S (Down)
    if (PW::is_key_down(win, PW::KEY_W)) {
        p1.move_up()
    }
    if (PW::is_key_down(win, PW::KEY_S)) {
        p1.move_down(screen_h)
    }

    // Player 2: Arrow Up / Arrow Down
    if (PW::is_key_down(win, PW::KEY_UP)) {
        p2.move_up()
    }
    if (PW::is_key_down(win, PW::KEY_DOWN)) {
        p2.move_down(screen_h)
    }

    // 4. Ball Physics
    ball.x = ball.x + ball.vx
    ball.y = ball.y + ball.vy

    // Wall Bounces (Top and Bottom)
    if (ball.y - ball.radius <= 10) {
        ball.y = 10 + ball.radius
        ball.vy = 0 - ball.vy
    }
    if (ball.y + ball.radius >= screen_h - 10) {
        ball.y = screen_h - 10 - ball.radius
        ball.vy = 0 - ball.vy
    }

    // Paddle Collisions (AABB Checks)
    // Left Paddle (P1)
    if (ball.vx < 0) {
        if (ball.x - ball.radius <= p1.x + p1.w) {
            if (ball.x + ball.radius >= p1.x) {
                if (ball.y >= p1.y) {
                    if (ball.y <= p1.y + p1.h) {
                        ball.vx = 0 - ball.vx
                        ball.x = p1.x + p1.w + ball.radius
                    }
                }
            }
        }
    }

    // Right Paddle (P2)
    if (ball.vx > 0) {
        if (ball.x + ball.radius >= p2.x) {
            if (ball.x - ball.radius <= p2.x + p2.w) {
                if (ball.y >= p2.y) {
                    if (ball.y <= p2.y + p2.h) {
                        ball.vx = 0 - ball.vx
                        ball.x = p2.x - ball.radius
                    }
                }
            }
        }
    }

    // Scoring & Reset
    if (ball.x < 0) {
        score_p2 = score_p2 + 1
        ball.reset(screen_w / 2, screen_h / 2, 4)
    }
    if (ball.x > screen_w) {
        score_p1 = score_p1 + 1
        ball.reset(screen_w / 2, screen_h / 2, -4)
    }

    // 5. Render Scene
    // Clear screen to deep retro blue
    PW::clear_buffer(buffer, screen_w, screen_h, 15, 18, 28)

    // Court Borders
    PW::draw_line(buffer, screen_w, screen_h, 10, 10, screen_w - 10, 10, 2, 60, 70, 90)
    PW::draw_line(buffer, screen_w, screen_h, 10, screen_h - 10, screen_w - 10, screen_h - 10, 2, 60, 70, 90)

    // Center Net Line
    net_y: i32 = 14
    for (net_y < screen_h - 14) {
        PW::draw_rect(buffer, screen_w, screen_h, (screen_w / 2) - 1, net_y, 2, 10, 1, true, 80, 90, 110)
        net_y = net_y + 18
    }

    // Use a Scoped Region for Frame Motion Trail
    region {
        trail_x: i32[] = [ball.x - ball.vx, ball.x - (ball.vx * 2)]
        trail_y: i32[] = [ball.y - ball.vy, ball.y - (ball.vy * 2)]
        PW::draw_circle(buffer, screen_w, screen_h, trail_x[0], trail_y[0], ball.radius - 1, 1, true, 220, 180, 60)
        PW::draw_circle(buffer, screen_w, screen_h, trail_x[1], trail_y[1], ball.radius - 3, 1, true, 160, 120, 30)
    }

    // Draw Paddles
    PW::draw_rect(buffer, screen_w, screen_h, p1.x, p1.y, p1.w, p1.h, 1, true, 80, 200, 255)
    PW::draw_rect(buffer, screen_w, screen_h, p2.x, p2.y, p2.w, p2.h, 1, true, 255, 100, 100)

    // Draw Ball
    PW::draw_circle(buffer, screen_w, screen_h, ball.x, ball.y, ball.radius, 1, true, 255, 230, 80)

    // Draw Scores
    PW::draw_text(buffer, screen_w, screen_h, f"{score_p1}", (screen_w / 2) - 60, 25, 24, 255, 255, 255)
    PW::draw_text(buffer, screen_w, screen_h, f"{score_p2}", (screen_w / 2) + 40, 25, 24, 255, 255, 255)

    // 6. Blit Buffer to Screen
    PW::update_window(win, buffer)
}

// Clean up
PW::destroy_window(win)
print("Thanks for playing Mylo Pong!")
```

---

## Running the Game

Launch the game with the Mylo interpreter:

```bash
./build/mylo pong.mylo
```

### Controls:
- **Player 1 (Left / Blue)**: `W` to move up, `S` to move down.
- **Player 2 (Right / Red)**: `Up Arrow` to move up, `Down Arrow` to move down.
- **Quit**: Press `Escape` or close the window.

---

## Challenges & Next Steps

Now that you have built Pong in Mylo, try customizing it!
1. **Speed Escalation**: Increase the ball speed (`ball.vx`) slightly every time a paddle successfully deflects it.
2. **Paddle Spin**: Modify `ball.vy` based on where the ball strikes the paddle (e.g. hitting the edge sends it flying at a sharper angle).
3. **Single Player AI**: Replace Player 2's keyboard input with simple computer tracking (`if (p2.y + (p2.h / 2) < ball.y) { p2.move_down(screen_h) }`).
4. **Persistent Arena High Scores**: Use `region highscores` and `highscores::scores : i32[] = ...` with file I/O (`read_lines` / `write_file`) to record top scores.
