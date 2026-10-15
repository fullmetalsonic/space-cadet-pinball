package com.codex.spacecadetpinball;

/** Converts window obstruction edges to additional padding for a possibly already-fitted content view. */
final class SurfaceSafeArea {
    static int[] padding(int windowWidth, int windowHeight, int contentX, int contentY,
                         int contentWidth, int contentHeight, int left, int top, int right, int bottom) {
        return new int[]{Math.max(0,left-contentX), Math.max(0,top-contentY),
            Math.max(0,contentX+contentWidth-(windowWidth-right)),
            Math.max(0,contentY+contentHeight-(windowHeight-bottom))};
    }
}
