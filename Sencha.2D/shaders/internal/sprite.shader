{
    "comment": "Pipeline metadata for the built-in sprite batcher (SpriteFeature).",

    "vertex":   "sprite.vert.glsl",
    "fragment": "sprite.frag.glsl",

    "vertex_inputs": [
        {
            "comment": "One instance-rate binding.  Stride must equal sizeof(SpriteFeature::GpuInstance) = 48.",
            "binding": 0,
            "stride":  48,
            "rate":    "instance"
        }
    ],

    "topology":      "triangle_list",
    "cull_mode":     "none",
    "front_face":    "ccw",
    "polygon_mode":  "fill",

    "depth_test":    false,
    "depth_write":   false,
    "depth_compare": "less_equal",

    "blend": [
        {
            "comment": "Straight-alpha blending over an opaque background.",
            "enable":    true,
            "src_color": "src_alpha",
            "dst_color": "one_minus_src_alpha",
            "color_op":  "add",
            "src_alpha": "one",
            "dst_alpha": "one_minus_src_alpha",
            "alpha_op":  "add"
        }
    ]
}
