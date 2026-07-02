component BobMotion {
    base_height: f32
    amplitude: f32 = 12.0
    frequency: f32 = 0.8
}

fn bob_offset(t: f32, frequency: f32, amplitude: f32) -> f32 {
    return sin(t * frequency * tau) * amplitude
}
