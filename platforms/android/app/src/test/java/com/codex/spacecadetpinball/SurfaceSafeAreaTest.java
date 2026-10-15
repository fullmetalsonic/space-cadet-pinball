package com.codex.spacecadetpinball;
import org.junit.Test;
import static org.junit.Assert.*;

public class SurfaceSafeAreaTest {
    @Test public void fullWindowSurfaceAvoidsVisibleBarsOnEveryEdge() {
        assertArrayEquals(new int[]{0,80,0,120},SurfaceSafeArea.padding(1248,1956,0,0,1248,1956,0,80,0,120));
    }
    @Test public void frameworkAlreadyFittedContentIsNotInsetTwice() {
        assertArrayEquals(new int[]{0,0,0,0},SurfaceSafeArea.padding(1248,1956,0,80,1248,1756,0,80,0,120));
    }
    @Test public void landscapeCutoutAndPartiallyFittedContentHaveIndependentEdges() {
        assertArrayEquals(new int[]{88,0,0,48},SurfaceSafeArea.padding(1956,1248,0,60,1956,1188,88,60,0,48));
    }
    @Test public void HidingBarsAndChangingWindowBoundsRecomputesInsteadOfAccumulating() {
        assertArrayEquals(new int[]{0,80,0,120},SurfaceSafeArea.padding(1248,1956,0,0,1248,1956,0,80,0,120));
        assertArrayEquals(new int[]{0,0,0,24},SurfaceSafeArea.padding(1248,1956,0,0,1248,1956,0,0,0,24));
        assertArrayEquals(new int[]{0,0,0,0},SurfaceSafeArea.padding(800,1000,0,48,800,904,0,48,0,48));
    }
    @Test public void imeAvoidanceSubtractsAnyFrameworkResizeInsteadOfShrinkingTwice() {
        assertArrayEquals(new int[]{0,136,0,800},SurfaceSafeArea.padding(1248,1972,0,0,1248,1972,0,136,0,800));
        assertArrayEquals(new int[]{0,0,0,674},SurfaceSafeArea.padding(1248,1972,0,136,1248,1710,0,136,0,800));
        assertArrayEquals(new int[]{0,0,0,0},SurfaceSafeArea.padding(1248,1972,0,136,1248,1036,0,136,0,800));
    }
    @Test public void imeHiddenRestoresBarBoundsAndAsymmetricEdgesStayPhysical() {
        assertArrayEquals(new int[]{88,60,24,500},SurfaceSafeArea.padding(1972,1248,0,0,1972,1248,88,60,24,500));
        assertArrayEquals(new int[]{88,60,24,48},SurfaceSafeArea.padding(1972,1248,0,0,1972,1248,88,60,24,48));
        assertArrayEquals(new int[]{24,60,88,500},SurfaceSafeArea.padding(1972,1248,0,0,1972,1248,24,60,88,500));
    }
}
