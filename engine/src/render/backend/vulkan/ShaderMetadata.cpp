#include <render/backend/vulkan/ShaderMetadata.h>

#include <core/json/JsonParser.h>
#include <core/json/JsonValue.h>

#include <stdexcept>
#include <string>
#include <unordered_map>

// ── JsonValue access helpers ──────────────────────────────────────────────────

namespace
{
    std::string GetStr(const JsonValue& obj, std::string_view key, const char* def)
    {
        const JsonValue* v = obj.Find(key);
        if (!v || !v->IsString()) return def;
        return v->AsString();
    }

    bool GetBool(const JsonValue& obj, std::string_view key, bool def)
    {
        const JsonValue* v = obj.Find(key);
        if (!v || !v->IsBool()) return def;
        return v->AsBool();
    }

    uint32_t GetUint(const JsonValue& obj, std::string_view key, uint32_t def)
    {
        const JsonValue* v = obj.Find(key);
        if (!v || !v->IsNumber()) return def;
        return static_cast<uint32_t>(v->AsNumber());
    }

// ── String → Vulkan enum tables ───────────────────────────────────────────────

    template <typename T>
    T Lookup(const std::unordered_map<std::string, T>& table,
             const std::string& key,
             const char* fieldName)
    {
        auto it = table.find(key);
        if (it == table.end())
        {
            throw std::invalid_argument(
                std::string("unknown value for '") + fieldName + "': \"" + key + "\"");
        }
        return it->second;
    }

    VkPrimitiveTopology ParseTopology(const std::string& s)
    {
        static const std::unordered_map<std::string, VkPrimitiveTopology> kTable = {
            { "point_list",                   VK_PRIMITIVE_TOPOLOGY_POINT_LIST                   },
            { "line_list",                    VK_PRIMITIVE_TOPOLOGY_LINE_LIST                    },
            { "line_strip",                   VK_PRIMITIVE_TOPOLOGY_LINE_STRIP                   },
            { "triangle_list",                VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST                },
            { "triangle_strip",               VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP               },
            { "triangle_fan",                 VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN                 },
            { "line_list_with_adjacency",     VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY     },
            { "line_strip_with_adjacency",    VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY    },
            { "triangle_list_with_adjacency", VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY },
            { "patch_list",                   VK_PRIMITIVE_TOPOLOGY_PATCH_LIST                   },
        };
        return Lookup(kTable, s, "topology");
    }

    VkCullModeFlags ParseCullMode(const std::string& s)
    {
        static const std::unordered_map<std::string, VkCullModeFlags> kTable = {
            { "none",           VK_CULL_MODE_NONE           },
            { "front",          VK_CULL_MODE_FRONT_BIT      },
            { "back",           VK_CULL_MODE_BACK_BIT       },
            { "front_and_back", VK_CULL_MODE_FRONT_AND_BACK },
        };
        return Lookup(kTable, s, "cull_mode");
    }

    VkFrontFace ParseFrontFace(const std::string& s)
    {
        static const std::unordered_map<std::string, VkFrontFace> kTable = {
            { "ccw", VK_FRONT_FACE_COUNTER_CLOCKWISE },
            { "cw",  VK_FRONT_FACE_CLOCKWISE         },
        };
        return Lookup(kTable, s, "front_face");
    }

    VkPolygonMode ParsePolygonMode(const std::string& s)
    {
        static const std::unordered_map<std::string, VkPolygonMode> kTable = {
            { "fill",  VK_POLYGON_MODE_FILL  },
            { "line",  VK_POLYGON_MODE_LINE  },
            { "point", VK_POLYGON_MODE_POINT },
        };
        return Lookup(kTable, s, "polygon_mode");
    }

    VkCompareOp ParseCompareOp(const std::string& s)
    {
        static const std::unordered_map<std::string, VkCompareOp> kTable = {
            { "never",         VK_COMPARE_OP_NEVER            },
            { "less",          VK_COMPARE_OP_LESS             },
            { "equal",         VK_COMPARE_OP_EQUAL            },
            { "less_equal",    VK_COMPARE_OP_LESS_OR_EQUAL    },
            { "greater",       VK_COMPARE_OP_GREATER          },
            { "not_equal",     VK_COMPARE_OP_NOT_EQUAL        },
            { "greater_equal", VK_COMPARE_OP_GREATER_OR_EQUAL },
            { "always",        VK_COMPARE_OP_ALWAYS           },
        };
        return Lookup(kTable, s, "depth_compare");
    }

    VkBlendFactor ParseBlendFactor(const std::string& s, const char* fieldName)
    {
        static const std::unordered_map<std::string, VkBlendFactor> kTable = {
            { "zero",                     VK_BLEND_FACTOR_ZERO                     },
            { "one",                      VK_BLEND_FACTOR_ONE                      },
            { "src_color",                VK_BLEND_FACTOR_SRC_COLOR                },
            { "one_minus_src_color",      VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR      },
            { "dst_color",                VK_BLEND_FACTOR_DST_COLOR                },
            { "one_minus_dst_color",      VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR      },
            { "src_alpha",                VK_BLEND_FACTOR_SRC_ALPHA                },
            { "one_minus_src_alpha",      VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA      },
            { "dst_alpha",                VK_BLEND_FACTOR_DST_ALPHA                },
            { "one_minus_dst_alpha",      VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA      },
            { "constant_color",           VK_BLEND_FACTOR_CONSTANT_COLOR           },
            { "one_minus_constant_color", VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR },
            { "constant_alpha",           VK_BLEND_FACTOR_CONSTANT_ALPHA           },
            { "one_minus_constant_alpha", VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA },
            { "src_alpha_saturate",       VK_BLEND_FACTOR_SRC_ALPHA_SATURATE       },
        };
        return Lookup(kTable, s, fieldName);
    }

    VkBlendOp ParseBlendOp(const std::string& s, const char* fieldName)
    {
        static const std::unordered_map<std::string, VkBlendOp> kTable = {
            { "add",              VK_BLEND_OP_ADD              },
            { "subtract",         VK_BLEND_OP_SUBTRACT         },
            { "reverse_subtract", VK_BLEND_OP_REVERSE_SUBTRACT },
            { "min",              VK_BLEND_OP_MIN              },
            { "max",              VK_BLEND_OP_MAX              },
        };
        return Lookup(kTable, s, fieldName);
    }

    ColorBlendAttachmentDesc ParseBlendAttachment(const JsonValue& j)
    {
        ColorBlendAttachmentDesc b{};
        b.BlendEnable = GetBool(j, "enable", false);

        if (b.BlendEnable)
        {
            b.SrcColor = ParseBlendFactor(GetStr(j, "src_color", "one"),  "src_color");
            b.DstColor = ParseBlendFactor(GetStr(j, "dst_color", "zero"), "dst_color");
            b.ColorOp  = ParseBlendOp(    GetStr(j, "color_op",  "add"),  "color_op");
            b.SrcAlpha = ParseBlendFactor(GetStr(j, "src_alpha", "one"),  "src_alpha");
            b.DstAlpha = ParseBlendFactor(GetStr(j, "dst_alpha", "zero"), "dst_alpha");
            b.AlphaOp  = ParseBlendOp(    GetStr(j, "alpha_op",  "add"),  "alpha_op");
        }
        else
        {
            b.SrcColor = VK_BLEND_FACTOR_ONE;
            b.DstColor = VK_BLEND_FACTOR_ZERO;
            b.ColorOp  = VK_BLEND_OP_ADD;
            b.SrcAlpha = VK_BLEND_FACTOR_ONE;
            b.DstAlpha = VK_BLEND_FACTOR_ZERO;
            b.AlphaOp  = VK_BLEND_OP_ADD;
        }

        b.WriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        return b;
    }

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

bool ParseShaderMetadataToDesc(std::string_view json,
                                 GraphicsPipelineDesc& out,
                                 std::string& error)
{
    JsonParseError parseError;
    auto root = JsonParse(json, &parseError);
    if (!root)
    {
        error = "JSON parse error at position " +
                std::to_string(parseError.Position) + ": " +
                parseError.Message;
        return false;
    }
    if (!root->IsObject())
    {
        error = "shader metadata root must be a JSON object";
        return false;
    }

    try
    {
        // -- Vertex input bindings -----------------------------------------
        if (const JsonValue* inputs = root->Find("vertex_inputs"))
        {
            if (!inputs->IsArray())
            {
                error = "'vertex_inputs' must be an array";
                return false;
            }
            for (const JsonValue& vi : inputs->AsArray())
            {
                if (!vi.IsObject()) continue;
                VertexInputBindingDesc b{};
                b.Binding  = GetUint(vi, "binding", 0u);
                b.Stride   = GetUint(vi, "stride",  0u);
                std::string rate = GetStr(vi, "rate", "vertex");
                b.InputRate = (rate == "instance") ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                   : VK_VERTEX_INPUT_RATE_VERTEX;
                out.VertexBindings.push_back(b);
            }
        }

        // -- Pipeline state ------------------------------------------------
        out.Topology    = ParseTopology(   GetStr(*root, "topology",     "triangle_list"));
        out.CullMode    = ParseCullMode(   GetStr(*root, "cull_mode",    "back"));
        out.FrontFace   = ParseFrontFace(  GetStr(*root, "front_face",   "ccw"));
        out.PolygonMode = ParsePolygonMode(GetStr(*root, "polygon_mode", "fill"));

        // -- Depth ---------------------------------------------------------
        out.DepthTest    = GetBool(*root, "depth_test",  false);
        out.DepthWrite   = GetBool(*root, "depth_write", false);
        out.DepthCompare = ParseCompareOp(GetStr(*root, "depth_compare", "less_equal"));

        // -- Blend ---------------------------------------------------------
        if (const JsonValue* blend = root->Find("blend"))
        {
            if (!blend->IsArray())
            {
                error = "'blend' must be an array";
                return false;
            }
            for (const JsonValue& ba : blend->AsArray())
            {
                if (!ba.IsObject()) continue;
                out.ColorBlend.push_back(ParseBlendAttachment(ba));
            }
        }
        else
        {
            // Default: one opaque pass-through attachment.
            ColorBlendAttachmentDesc def{};
            def.BlendEnable = false;
            def.SrcColor    = VK_BLEND_FACTOR_ONE;
            def.DstColor    = VK_BLEND_FACTOR_ZERO;
            def.ColorOp     = VK_BLEND_OP_ADD;
            def.SrcAlpha    = VK_BLEND_FACTOR_ONE;
            def.DstAlpha    = VK_BLEND_FACTOR_ZERO;
            def.AlphaOp     = VK_BLEND_OP_ADD;
            def.WriteMask   =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            out.ColorBlend.push_back(def);
        }

        return true;
    }
    catch (const std::invalid_argument& e)
    {
        error = std::string("shader metadata error: ") + e.what();
        return false;
    }
}
