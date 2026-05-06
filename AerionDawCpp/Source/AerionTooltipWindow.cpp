#include "AerionTooltipWindow.h"

AerionTooltipWindow::AerionTooltipWindow (juce::Component* parentComp, int delayMs)
    : juce::Component ("tooltip"),
      millisecondsBeforeTipAppears (delayMs)
{
    setAlwaysOnTop (true);
    setOpaque (true);
    setAccessible (false);

    if (parentComp != nullptr)
        parentComp->addChildComponent (this);

    auto& desktop = juce::Desktop::getInstance();
    if (desktop.getMainMouseSource().canHover())
    {
        desktop.addGlobalMouseListener (this);
        startTimer (50); // ~20 Hz: responsive hover without hammering the message thread
    }
}

AerionTooltipWindow::~AerionTooltipWindow()
{
    hideTip();
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
}

void AerionTooltipWindow::setMillisecondsBeforeTipAppears (const int newTimeMs) noexcept
{
    millisecondsBeforeTipAppears = newTimeMs;
}

void AerionTooltipWindow::paint (juce::Graphics& g)
{
    getLookAndFeel().drawTooltip (g, tipShowing, getWidth(), getHeight());
}

void AerionTooltipWindow::mouseEnter (const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
        hideTip();
}

void AerionTooltipWindow::mouseDown (const juce::MouseEvent&)
{
    if (isVisible())
        dismissalMouseEventOccurred = true;
}

void AerionTooltipWindow::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&)
{
    if (isVisible())
        dismissalMouseEventOccurred = true;
}

void AerionTooltipWindow::updatePosition (const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parentArea)
{
    setBounds (getLookAndFeel().getTooltipBounds (tip, pos, parentArea));
    setVisible (true);
}

void AerionTooltipWindow::displayTip (juce::Point<int> screenPos, const juce::String& tip)
{
    jassert (tip.isNotEmpty());
    displayTipInternal (screenPos, tip, ShownManually::yes);
}

void AerionTooltipWindow::displayTipInternal (juce::Point<int> screenPos, const juce::String& tip, ShownManually shownManually)
{
    if (reentrant)
        return;

    const juce::ScopedValueSetter<bool> guard (reentrant, true, false);

    if (tipShowing != tip)
    {
        tipShowing = tip;
        repaint();
    }

    auto* parent = getParentComponent();
    jassert (parent != nullptr);
    if (parent == nullptr)
        return;

    updatePosition (tip, parent->getLocalPoint (nullptr, screenPos), parent->getLocalBounds());

    toFront (false);
    manuallyShownTip = shownManually == ShownManually::yes ? tip : juce::String();
    dismissalMouseEventOccurred = false;
}

juce::String AerionTooltipWindow::getTipFor (juce::Component& c)
{
    if (juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown())
        return {};

    if (c.isCurrentlyBlockedByAnotherModalComponent())
        return {};

    if (auto* ttc = dynamic_cast<juce::TooltipClient*> (&c))
        return ttc->getTooltip();

    return {};
}

void AerionTooltipWindow::hideTip()
{
    if (isVisible() && ! reentrant)
    {
        tipShowing = {};
        manuallyShownTip = {};
        dismissalMouseEventOccurred = false;

        removeFromDesktop();
        setVisible (false);

        lastHideTime = juce::Time::getApproximateMillisecondCounter();
    }
}

float AerionTooltipWindow::getDesktopScaleFactor() const
{
    if (lastComponentUnderMouse != nullptr)
        return juce::Component::getApproximateScaleFactorForComponent (lastComponentUnderMouse);

    return juce::Component::getDesktopScaleFactor();
}

std::unique_ptr<juce::AccessibilityHandler> AerionTooltipWindow::createAccessibilityHandler()
{
    return createIgnoredAccessibilityHandler (*this);
}

void AerionTooltipWindow::timerCallback()
{
    const auto mouseSource = juce::Desktop::getInstance().getMainMouseSource();
    auto* newComp = mouseSource.isTouch() ? nullptr : mouseSource.getComponentUnderMouse();

    if (manuallyShownTip.isNotEmpty())
    {
        if (dismissalMouseEventOccurred || newComp == nullptr)
            hideTip();

        return;
    }

    auto* parent = getParentComponent();
    if (parent == nullptr)
    {
        hideTip();
        return;
    }

    // Parented tooltips: ignore hover targeting components on a different peer (other window).
    // Compare the main editor peer, not the tooltip component's peer (can be null on some hosts).
    if (newComp != nullptr)
    {
        auto* hostPeer = parent->getPeer();
        auto* underPeer = newComp->getPeer();
        if (hostPeer != nullptr && underPeer != nullptr && hostPeer != underPeer)
            return;
    }

    const auto newTip = newComp != nullptr ? getTipFor (*newComp) : juce::String();
    const auto mousePos = mouseSource.getScreenPosition();
    const auto mouseMovedQuickly = (mousePos.getDistanceFrom (lastMousePos) > 12.0f);
    lastMousePos = mousePos;

    const auto tipChanged = (newTip != lastTipUnderMouse || lastComponentUnderMouse != newComp);
    const auto now = juce::Time::getApproximateMillisecondCounter();

    lastComponentUnderMouse = newComp;
    lastTipUnderMouse = newTip;

    if (tipChanged || dismissalMouseEventOccurred || mouseMovedQuickly)
        lastCompChangeTime = now;

    if (newComp == nullptr || dismissalMouseEventOccurred || newTip.isEmpty())
    {
        hideTip();
        return;
    }

    if (tipChanged && isVisible())
        hideTip();

    if (! isVisible()
        && now > lastCompChangeTime + (juce::uint32) millisecondsBeforeTipAppears)
        displayTipInternal (mousePos.roundToInt(), newTip, ShownManually::no);
}
