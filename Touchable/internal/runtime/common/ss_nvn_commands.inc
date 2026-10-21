/* Additional command forms use the same replay records as upstream. */
NVN(nvnCommandBufferSetViewport, void, NVNcommandBuffer *command, int x, int y, int width, int height) {
    float viewport[4] = {x, y, width, height};
    h_nvnCommandBufferSetViewports(command, 0, 1, viewport);
}
NVN(nvnCommandBufferDrawElements, void, NVNcommandBuffer *command, NVNdrawPrimitive primitive, NVNindexType type, int count, NVNbufferAddress indices) {
    h_nvnCommandBufferDrawElementsInstanced(command, primitive, type, count, indices, 0, 0, 1);
}
NVN(nvnCommandBufferDrawElementsBaseVertex, void, NVNcommandBuffer *command, NVNdrawPrimitive primitive, NVNindexType type, int count, NVNbufferAddress indices, int base) {
    h_nvnCommandBufferDrawElementsInstanced(command, primitive, type, count, indices, base, 0, 1);
}
NVN(nvnCommandBufferBindTexture, void, NVNcommandBuffer *command, NVNshaderStage stage, int index, NVNtextureHandle handle) {
    bind_handles(command, OP_BIND_TEX, stage, index, 1, &handle);
}
NVN(nvnCommandBufferBindTextures, void, NVNcommandBuffer *command, NVNshaderStage stage, int index, int count, const NVNtextureHandle *handles) {
    bind_handles(command, OP_BIND_TEX, stage, index, count, handles);
}
NVN(nvnCommandBufferBindSeparateTexture, void, NVNcommandBuffer *command, NVNshaderStage stage, int index, NVNseparateTextureHandle handle) {
    h_nvnCommandBufferBindSeparateTextures(command, stage, index, 1, &handle);
}
NVN(nvnCommandBufferBindImage, void, NVNcommandBuffer *command, NVNshaderStage stage, int index, NVNimageHandle handle) {
    h_nvnCommandBufferBindImages(command, stage, index, 1, &handle);
}
/* Metal clamps depth instead of clipping when the game enables depth clamp. */
NVN(nvnCommandBufferSetDepthClamp, void, NVNcommandBuffer *command, uint32_t enable) {
    *REC(command, OP_DEPTH_CLAMP, p_depth_clamp) = (p_depth_clamp){B(enable), 0};
}
NVN(nvnCommandBufferBindVertexBuffer, void, NVNcommandBuffer *command, int index, NVNbufferAddress address, size_t size) {
    NVNbufferRange range = {address, size};
    h_nvnCommandBufferBindVertexBuffers(command, index, 1, &range);
}
/* The replay keeps one scissor rectangle. Extra rectangles only matter for viewport arrays. */
NVN(nvnCommandBufferSetScissors, void, NVNcommandBuffer *command, int first, int count, const int *rectangles) {
    if (first < 0 || count < 0) die("Invalid scissor range");
    if (first == 0 && count > 0) h_nvnCommandBufferSetScissor(command, rectangles[0], rectangles[1], rectangles[2], rectangles[3]);
    if (first + count > 1) { static int said; if (!said++) LOG("[nvn] SetScissors uses scissor 0 only (first %d, count %d)", first, count); }
}
