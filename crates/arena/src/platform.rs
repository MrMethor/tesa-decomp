//! Window, keyboard and frame pacing (winit + softbuffer).

use std::num::NonZeroU32;
use std::rc::Rc;
use std::time::{Duration, Instant};

use winit::application::ApplicationHandler;
use winit::dpi::LogicalSize;
use winit::event::{ElementState, KeyEvent, MouseButton, WindowEvent};
use winit::event_loop::{ActiveEventLoop, ControlFlow, EventLoop};
use winit::keyboard::{KeyCode, PhysicalKey};
use winit::window::{Window, WindowId};

use arena_render_classic::{SCREEN_H, SCREEN_W};
use arena_sim::game::{TICKS_PER_FRAME, TICK_HZ};
use arena_sim::KeyState;

use crate::Session;

/// PC scancode of a key, as the original's key table indexes it.
fn scancode(code: KeyCode) -> Option<u8> {
    Some(match code {
        KeyCode::Escape => 0x01,
        KeyCode::Tab => 0x0F,
        KeyCode::Enter => 0x1C,
        KeyCode::ControlLeft | KeyCode::ControlRight => 0x1D,
        KeyCode::ShiftLeft => 0x2A,
        KeyCode::ShiftRight => 0x36,
        KeyCode::AltLeft | KeyCode::AltRight => 0x38,
        KeyCode::Space => 0x39,
        KeyCode::ArrowUp => 0x48,
        KeyCode::ArrowLeft => 0x4B,
        KeyCode::ArrowRight => 0x4D,
        KeyCode::ArrowDown => 0x50,
        _ => return None,
    })
}

struct App {
    session: Session,
    window: Option<Rc<Window>>,
    surface: Option<softbuffer::Surface<Rc<Window>, Rc<Window>>>,
    keys: KeyState,
    /// Cursor in window pixels.
    cursor: (f64, f64),
    frame_time: Duration,
    next_frame: Instant,
    error: Option<String>,
}

impl App {
    /// The picture's place in the window: offset and size of the 4:3 area.
    fn picture_rect(&self) -> Option<(usize, usize, usize, usize)> {
        let size = self.window.as_ref()?.inner_size();
        let (ww, wh) = (size.width as usize, size.height as usize);
        if ww == 0 || wh == 0 {
            return None;
        }
        let (pw, ph) = if ww * 3 >= wh * 4 { (wh * 4 / 3, wh) } else { (ww, ww * 3 / 4) };
        Some(((ww - pw) / 2, (wh - ph) / 2, pw, ph))
    }

    /// Window pixel to 320 x 200 frame pixel.
    fn to_screen(&self, x: f64, y: f64) -> Option<(usize, usize)> {
        let (ox, oy, pw, ph) = self.picture_rect()?;
        let (fx, fy) = ((x - ox as f64) / pw as f64, (y - oy as f64) / ph as f64);
        ((0.0..1.0).contains(&fx) && (0.0..1.0).contains(&fy))
            .then_some(((fx * SCREEN_W as f64) as usize, (fy * SCREEN_H as f64) as usize))
    }

    fn present(&mut self) -> Result<(), String> {
        let (Some(window), Some(surface)) = (self.window.as_ref(), self.surface.as_mut()) else {
            return Ok(());
        };
        let size = window.inner_size();
        let (Some(w), Some(h)) = (NonZeroU32::new(size.width), NonZeroU32::new(size.height)) else {
            return Ok(());
        };
        surface.resize(w, h).map_err(|e| e.to_string())?;
        let screen = *self.session.draw();
        let palette = *self.session.palette();
        let mut buffer = surface.buffer_mut().map_err(|e| e.to_string())?;
        let (ww, wh) = (w.get() as usize, h.get() as usize);
        // Fit a 4:3 picture (320 x 200 shown with tall pixels) into the window.
        let (pw, ph) = if ww * 3 >= wh * 4 { (wh * 4 / 3, wh) } else { (ww, ww * 3 / 4) };
        let (ox, oy) = ((ww - pw) / 2, (wh - ph) / 2);
        buffer.fill(0);
        for y in 0..ph {
            let sy = y * SCREEN_H / ph;
            let row = &mut buffer[(oy + y) * ww + ox..(oy + y) * ww + ox + pw];
            for (x, px) in row.iter_mut().enumerate() {
                *px = palette[screen[sy * SCREEN_W + x * SCREEN_W / pw] as usize];
            }
        }
        buffer.present().map_err(|e| e.to_string())
    }
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        if self.window.is_some() {
            return;
        }
        let attrs = Window::default_attributes()
            .with_title("Arena (port)")
            .with_inner_size(LogicalSize::new(960.0, 720.0));
        let result = (|| -> Result<(), String> {
            let window = Rc::new(event_loop.create_window(attrs).map_err(|e| e.to_string())?);
            let context = softbuffer::Context::new(window.clone()).map_err(|e| e.to_string())?;
            let surface = softbuffer::Surface::new(&context, window.clone()).map_err(|e| e.to_string())?;
            self.window = Some(window);
            self.surface = Some(surface);
            Ok(())
        })();
        if let Err(e) = result {
            self.error = Some(e);
            event_loop.exit();
        }
        self.next_frame = Instant::now();
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, _id: WindowId, event: WindowEvent) {
        match event {
            WindowEvent::CloseRequested => event_loop.exit(),
            WindowEvent::KeyboardInput {
                event: KeyEvent { physical_key: PhysicalKey::Code(code), state, repeat: false, .. },
                ..
            } => {
                if code == KeyCode::Escape {
                    event_loop.exit();
                }
                if code == KeyCode::KeyE && state == ElementState::Pressed {
                    self.session.activate_ahead();
                }
                if let Some(sc) = scancode(code) {
                    match state {
                        ElementState::Pressed => self.keys.press(sc),
                        ElementState::Released => self.keys.release(sc),
                    }
                }
            }
            WindowEvent::Focused(false) => self.keys = KeyState::default(),
            WindowEvent::CursorMoved { position, .. } => self.cursor = (position.x, position.y),
            WindowEvent::MouseInput { state: ElementState::Pressed, button: MouseButton::Left, .. } => {
                if let Some((x, y)) = self.to_screen(self.cursor.0, self.cursor.1) {
                    self.session.click(x, y);
                }
            }
            WindowEvent::RedrawRequested => {
                if let Err(e) = self.present() {
                    self.error = Some(e);
                    event_loop.exit();
                }
            }
            _ => {}
        }
    }

    fn about_to_wait(&mut self, event_loop: &ActiveEventLoop) {
        let now = Instant::now();
        if now >= self.next_frame {
            // One original frame per two timer ticks; if we fell far behind
            // (window dragged, debugger), do not try to catch up.
            self.session.step(self.keys);
            self.next_frame += self.frame_time;
            if now > self.next_frame + self.frame_time * 4 {
                self.next_frame = now + self.frame_time;
            }
            if let Some(w) = &self.window {
                w.request_redraw();
            }
        }
        event_loop.set_control_flow(ControlFlow::WaitUntil(self.next_frame));
    }
}

pub fn run(session: Session) -> Result<(), String> {
    let event_loop = EventLoop::new().map_err(|e| e.to_string())?;
    let mut app = App {
        session,
        window: None,
        surface: None,
        keys: KeyState::default(),
        cursor: (0.0, 0.0),
        frame_time: Duration::from_secs_f64(TICKS_PER_FRAME as f64 / TICK_HZ),
        next_frame: Instant::now(),
        error: None,
    };
    event_loop.run_app(&mut app).map_err(|e| e.to_string())?;
    app.error.map_or(Ok(()), Err)
}
