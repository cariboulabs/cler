// Core-nodes exact connection drawing implementation
// Copied word-for-word from core-nodes/CoreDiagram.cpp

#define IMGUI_DEFINE_MATH_OPERATORS
#include "flow_canvas.hpp"
#include <imgui.h>
#include <imgui_internal.h>  // For ImRect
#include <set>
#include <algorithm>

namespace clerflow {

// Helper functions for link separation
void FlowCanvas::SetInputSepUp(Connection& conn) const {
    conn.xSepIn = 20.0f * zoom;
}

void FlowCanvas::SetInputSepDown(Connection& conn) const {
    conn.xSepIn = -20.0f * zoom;
}

void FlowCanvas::SetOutputSepUp(Connection& conn) const {
    conn.xSepOut = 20.0f * zoom;
}

void FlowCanvas::SetOutputSepDown(Connection& conn) const {
    conn.xSepOut = -20.0f * zoom;
}

void FlowCanvas::SetNodeSep(Connection& conn, const VisualNode* from_node, const VisualNode* to_node) const {
    // Count unique connections between these two nodes
    int numberOfUniqueLines = 1;  // Simplified for now
    conn.ykSep = numberOfUniqueLines;
}

void FlowCanvas::SetLinkProperties(Connection& conn, const VisualNode* from_node, const VisualNode* to_node) {
    // Initialize link properties
    conn.link_type = LinkType::NONE;
    conn.xSepIn = 15.0f * zoom;  // Default separation
    conn.xSepOut = 15.0f * zoom;
    conn.ykSep = 0;
    
    // Get node rectangles in screen space
    ImVec2 from_min = CanvasToScreen(from_node->position);
    ImVec2 from_max = CanvasToScreen(ImVec2(from_node->position.x + from_node->size.x, 
                                             from_node->position.y + from_node->size.y));
    ImVec2 to_min = CanvasToScreen(to_node->position);
    ImVec2 to_max = CanvasToScreen(ImVec2(to_node->position.x + to_node->size.x, 
                                          to_node->position.y + to_node->size.y));
    
    ImRect rInput(to_min, to_max);    // Input node (destination)
    ImRect rOutput(from_min, from_max); // Output node (source)
    
    // Get port positions
    float yInput = to_min.y + to_node->size.y * 0.5f;  // Approximate center
    float yOutput = from_min.y + from_node->size.y * 0.5f;
    
    float yMargin = 30.0f * zoom;
    bool inputNodeInverted = false;  // We don't have inverted ports in our system
    bool outputNodeInverted = false;
    
    if (inputNodeInverted && outputNodeInverted) {
        if (rInput.Max.x <= rOutput.Min.x) {
            conn.link_type = LinkType::BINV_LEFT;
        }
        if (rInput.Max.x > rOutput.Min.x) {
            float nodeMargin = 24.0f * zoom;
            if (rInput.Max.y + nodeMargin < rOutput.Min.y) {
                conn.link_type = LinkType::BINV_RIGHT_OVER;
                SetOutputSepUp(conn);
                SetInputSepDown(conn);
                SetNodeSep(conn, from_node, to_node);
            }
            else if (rInput.Min.y > rOutput.Max.y + nodeMargin) {
                conn.link_type = LinkType::BINV_RIGHT_UNDER;
                SetOutputSepDown(conn);
                SetInputSepUp(conn);
                SetNodeSep(conn, from_node, to_node);
            }
            else {
                conn.link_type = LinkType::BINV_RIGHT_MID;
                SetOutputSepDown(conn);
                SetInputSepDown(conn);
                SetNodeSep(conn, from_node, to_node);
            }
        }
    }
    else if (inputNodeInverted) {
        if (yInput + yMargin < yOutput) {
            if (ImMax(rInput.Max.x, rOutput.Max.x) == rInput.Max.x) {
                conn.link_type = LinkType::IINV_RIGHT_OVER;
                SetInputSepDown(conn);
            }
            else if (ImMax(rInput.Max.x, rOutput.Max.x) == rOutput.Max.x) {
                conn.link_type = LinkType::IINV_LEFT_OVER;
                SetOutputSepUp(conn);
            }
        }
        else if (yInput > yOutput + yMargin) {
            if (ImMax(rInput.Max.x, rOutput.Max.x) == rInput.Max.x) {
                conn.link_type = LinkType::IINV_RIGHT_UNDER;
                SetInputSepUp(conn);
            }
            else if (ImMax(rInput.Max.x, rOutput.Max.x) == rOutput.Max.x) {
                conn.link_type = LinkType::IINV_LEFT_UNDER;
                SetOutputSepDown(conn);
            }
        }
        else {
            conn.link_type = LinkType::IINV_MID;
        }
    }
    else if (outputNodeInverted) {
        if (yInput + yMargin < yOutput) {
            if (ImMax(rInput.Min.x, rOutput.Min.x) == rInput.Min.x) {
                conn.link_type = LinkType::OINV_RIGHT_OVER;
                SetOutputSepUp(conn);
            }
            else if (ImMax(rInput.Min.x, rOutput.Min.x) == rOutput.Min.x) {
                conn.link_type = LinkType::OINV_LEFT_OVER;
                SetInputSepDown(conn);
            }
        }
        else if (yInput > yOutput + yMargin) {
            if (ImMax(rInput.Min.x, rOutput.Min.x) == rInput.Min.x) {
                conn.link_type = LinkType::OINV_RIGHT_UNDER;
                SetOutputSepDown(conn);
            }
            else if (ImMax(rInput.Min.x, rOutput.Min.x) == rOutput.Min.x) {
                conn.link_type = LinkType::OINV_LEFT_UNDER;
                SetInputSepUp(conn);
            }
        }
        else {
            conn.link_type = LinkType::OINV_MID;
        }
    }
    else if (rInput.Min.x >= rOutput.Max.x) {
        conn.link_type = LinkType::NINV_RIGHT;
    }
    else if (rInput.Min.x < rOutput.Max.x) {
        float nodeMargin = 24.0f * zoom;
        if (rInput.Max.y + nodeMargin < rOutput.Min.y) {
            conn.link_type = LinkType::NINV_LEFT_OVER;
            SetInputSepDown(conn);
            SetOutputSepUp(conn);
            SetNodeSep(conn, from_node, to_node);
        }
        else if (rInput.Min.y > rOutput.Max.y + nodeMargin) {
            conn.link_type = LinkType::NINV_LEFT_UNDER;
            SetInputSepUp(conn);
            SetOutputSepDown(conn);
            SetNodeSep(conn, from_node, to_node);
        }
        else {
            conn.link_type = LinkType::NINV_LEFT_MID;
            SetOutputSepDown(conn);
            SetInputSepDown(conn);
            SetNodeSep(conn, from_node, to_node);
        }
    }
}

void FlowCanvas::DrawLinkBezier(const Connection& conn, ImVec2 pInput, ImVec2 pOutput, float rounding, bool invert) const {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 handle = invert ? ImVec2(rounding, 0.0f) * zoom : ImVec2(-rounding, 0.0f) * zoom;
    ImVec2 p1 = pInput;
    ImVec2 p2 = pInput + handle;
    ImVec2 p3 = pOutput - handle;
    ImVec2 p4 = pOutput;
    
    ImU32 color = dataTypeToColor(conn.data_type);
    float thickness = 3.0f * zoom;
    
    drawList->AddBezierCubic(p1, p2, p3, p4, color, thickness);
}

void FlowCanvas::DrawLinkIOInv(const Connection& conn, ImVec2 pInput, ImVec2 pOutput, float dHandle) const {
    float xMax = ImMax(pInput.x, pOutput.x);
    float xMin = ImMin(pInput.x, pOutput.x);
    float xMargin = dHandle;
    float x1 = 0.0f;
    float x2 = 0.0f;
    
    // Input inverted.
    if (conn.link_type == LinkType::IINV_LEFT_OVER || conn.link_type == LinkType::IINV_LEFT_UNDER) {
        x1 = xMax - xMargin + conn.xSepOut;
        x2 = x1 + dHandle;
    }
    if (conn.link_type == LinkType::IINV_RIGHT_OVER || conn.link_type == LinkType::IINV_RIGHT_UNDER) {
        x1 = xMax - xMargin + conn.xSepIn;
        x2 = x1 + dHandle;
    }
    // Output inverted.
    if (conn.link_type == LinkType::OINV_LEFT_OVER || conn.link_type == LinkType::OINV_LEFT_UNDER) {
        x1 = xMin + xMargin - conn.xSepIn;
        x2 = x1 - dHandle;
    }
    if (conn.link_type == LinkType::OINV_RIGHT_OVER || conn.link_type == LinkType::OINV_RIGHT_UNDER) {
        x1 = xMin + xMargin - conn.xSepOut;
        x2 = x1 - dHandle;
    }
    
    // Input is over the output.
    if (conn.link_type == LinkType::IINV_LEFT_OVER || conn.link_type == LinkType::IINV_RIGHT_OVER ||
        conn.link_type == LinkType::OINV_LEFT_OVER || conn.link_type == LinkType::OINV_RIGHT_OVER) {
        dHandle *= +1;
    }
    // Input is under the output.
    if (conn.link_type == LinkType::IINV_LEFT_UNDER || conn.link_type == LinkType::IINV_RIGHT_UNDER ||
        conn.link_type == LinkType::OINV_LEFT_UNDER || conn.link_type == LinkType::OINV_RIGHT_UNDER) {
        dHandle *= -1;
    }
    
    float y1 = pInput.y + dHandle;
    float y2 = pOutput.y - dHandle;
    
    std::vector<ImVec2> points;
    points.reserve(8);
    points.push_back(pInput);
    points.push_back(ImVec2(x1, pInput.y));
    points.push_back(ImVec2(x2, pInput.y));
    points.push_back(ImVec2(x2, y1));
    points.push_back(ImVec2(x2, y2));
    points.push_back(ImVec2(x2, pOutput.y));
    points.push_back(ImVec2(x1, pOutput.y));
    points.push_back(pOutput);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 color = dataTypeToColor(conn.data_type);
    float thickness = 3.0f * zoom;
    
    drawList->AddBezierQuadratic(points[0], (points[0] + points[1]) * 0.5f, points[1], color, thickness);
    drawList->AddBezierQuadratic(points[1], points[2], points[3], color, thickness);
    drawList->AddBezierQuadratic(points[3], (points[3] + points[4]) * 0.5f, points[4], color, thickness);
    drawList->AddBezierQuadratic(points[4], points[5], points[6], color, thickness);
    drawList->AddBezierQuadratic(points[6], (points[6] + points[7]) * 0.5f, points[7], color, thickness);
}

void FlowCanvas::DrawLinkBNInv(const Connection& conn, ImVec2 pInput, ImVec2 pOutput, float dHandle, bool invert) const {
    // Get node bounds for proper routing
    const VisualNode* from_node = GetNode(conn.from_node_id);
    const VisualNode* to_node = GetNode(conn.to_node_id);
    if (!from_node || !to_node) return;
    
    ImVec2 from_min = CanvasToScreen(from_node->position);
    ImVec2 from_max = CanvasToScreen(ImVec2(from_node->position.x + from_node->size.x, 
                                             from_node->position.y + from_node->size.y));
    ImVec2 to_min = CanvasToScreen(to_node->position);
    ImVec2 to_max = CanvasToScreen(ImVec2(to_node->position.x + to_node->size.x, 
                                          to_node->position.y + to_node->size.y));
    
    ImRect rInputNode(to_min, to_max);
    ImRect rOutputNode(from_min, from_max);
    
    // Count unique lines between two nodes (simplified)
    int numberOfUniqueLines = 1;
    
    float x1 = 0;
    float x2 = 0;
    float x3 = 0;
    float x4 = 0;
    float xMargin = dHandle;
    
    if (invert == false) {
        x1 = pInput.x + xMargin - conn.xSepIn;
        x2 = x1 - dHandle;
        x3 = pOutput.x - xMargin + conn.xSepOut;
        x4 = x3 + dHandle;
    }
    else {
        x1 = pInput.x - xMargin + conn.xSepIn;
        x2 = x1 + dHandle;
        x3 = pOutput.x + xMargin - conn.xSepOut;
        x4 = x3 - dHandle;
    }
    
    float yM = 0;
    float y1 = 0;
    float y2 = 0;
    float y3 = 0;
    float y4 = 0;
    float y5 = 0;
    
    if (conn.link_type == LinkType::NINV_LEFT_OVER || conn.link_type == LinkType::BINV_RIGHT_OVER) {
        dHandle *= +1;
        yM = rInputNode.Max.y + conn.ykSep * (rOutputNode.Min.y - rInputNode.Max.y) / float(numberOfUniqueLines + 1);
        y1 = pInput.y + dHandle;
        y2 = yM - dHandle;
        y3 = y2 + dHandle;
        y4 = y3 + dHandle;
        y5 = pOutput.y - dHandle;
    }
    else if (conn.link_type == LinkType::NINV_LEFT_UNDER || conn.link_type == LinkType::BINV_RIGHT_UNDER) {
        dHandle *= -1;
        yM = rOutputNode.Max.y + conn.ykSep * (rInputNode.Min.y - rOutputNode.Max.y) / float(numberOfUniqueLines + 1);
        y1 = pInput.y + dHandle;
        y2 = yM - dHandle;
        y3 = y2 + dHandle;
        y4 = y3 + dHandle;
        y5 = pOutput.y - dHandle;
    }
    else if (conn.link_type == LinkType::NINV_LEFT_MID || conn.link_type == LinkType::BINV_RIGHT_MID) {
        float yMax = ImMax(rInputNode.Max.y, rOutputNode.Max.y);
        float yMargin = 4.0f * zoom;
        yM = yMax + (yMargin + dHandle) * conn.ykSep;
        y1 = pInput.y + dHandle;
        y2 = yM - dHandle;
        y3 = y2 + dHandle;
        y4 = y3 - dHandle;
        y5 = pOutput.y + dHandle;
    }
    
    std::vector<ImVec2> points;
    points.reserve(14);
    points.push_back(pInput);
    points.push_back(ImVec2(x1, pInput.y));
    points.push_back(ImVec2(x2, pInput.y));
    points.push_back(ImVec2(x2, y1));
    points.push_back(ImVec2(x2, y2));
    points.push_back(ImVec2(x2, y3));
    points.push_back(ImVec2(x1, y3));
    points.push_back(ImVec2(x3, y3));
    points.push_back(ImVec2(x4, y3));
    points.push_back(ImVec2(x4, y4));
    points.push_back(ImVec2(x4, y5));
    points.push_back(ImVec2(x4, pOutput.y));
    points.push_back(ImVec2(x3, pOutput.y));
    points.push_back(pOutput);
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 color = dataTypeToColor(conn.data_type);
    float thickness = 3.0f * zoom;
    
    drawList->AddBezierQuadratic(points[0], (points[0] + points[1]) * 0.5f, points[1], color, thickness);
    drawList->AddBezierQuadratic(points[1], points[2], points[3], color, thickness);
    drawList->AddBezierQuadratic(points[3], (points[3] + points[4]) * 0.5f, points[4], color, thickness);
    drawList->AddBezierQuadratic(points[4], points[5], points[6], color, thickness);
    drawList->AddBezierQuadratic(points[6], (points[6] + points[7]) * 0.5f, points[7], color, thickness);
    drawList->AddBezierQuadratic(points[7], points[8], points[9], color, thickness);
    drawList->AddBezierQuadratic(points[9], (points[9] + points[10]) * 0.5f, points[10], color, thickness);
    drawList->AddBezierQuadratic(points[10], points[11], points[12], color, thickness);
    drawList->AddBezierQuadratic(points[12], (points[12] + points[13]) * 0.5f, points[13], color, thickness);
}

} // namespace clerflow