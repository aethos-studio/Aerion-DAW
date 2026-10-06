#pragma once

// Screen-reader and keyboard access for controls a view paints itself.
//
// Most of Aerion's controls are drawn by their view and found by a rectangle
// test in its mouse handlers. An Accessibility::Proxy is an invisible child
// laid over one of them: it gives the control a title, a role and a value for
// screen readers, takes keyboard focus (Tab), and works the control from the
// keyboard (Space / Return press it; Up / Down, Page Up / Page Down and Home
// change a slider). Mouse input passes through to the view unchanged.

#include <JuceHeader.h>
#include "ThemeTokens.h"
#include <map>
#include <vector>

namespace Accessibility
{
    enum class Role { button, toggle, slider };

    struct Control
    {
        juce::String id;     // stable across layouts, so focus survives a resync
        juce::String title;
        Role role = Role::button;
        juce::Rectangle<int> bounds;

        std::function<void()> press;          // button, toggle
        std::function<bool()> isOn;           // toggle

        std::function<double()> getValue;     // slider
        std::function<void (double)> setValue;
        juce::Range<double> range { 0.0, 1.0 };
        double step = 0.01, fineStep = 0.001, bigStep = 0.1, resetValue = 0.0;
        std::function<juce::String (double)> valueText;
    };

    /** Presses whatever `owner` draws at `point`, through its own mouse handlers. */
    inline void clickAt (juce::Component& owner, juce::Point<int> point, bool rightButton = false)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto pos = point.toFloat();
        auto event = [&] (juce::ModifierKeys mods)
        {
            return juce::MouseEvent (source, pos, mods, juce::MouseInputSource::defaultPressure,
                                     juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                                     juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &owner, &owner, now, pos, now, 1, false);
        };

        owner.mouseDown (event (juce::ModifierKeys (rightButton ? juce::ModifierKeys::rightButtonModifier
                                                                : juce::ModifierKeys::leftButtonModifier)));
        owner.mouseUp (event ({}));
    }

    class Proxy : public juce::Component
    {
    public:
        Proxy()
        {
            setInterceptsMouseClicks (false, false);
            setWantsKeyboardFocus (true);
        }

        void update (Control c)
        {
            const bool titleChanged = c.title != control.title;
            control = std::move (c);
            setTitle (control.title);
            setBounds (control.bounds);

            if (titleChanged)
                if (auto* h = getAccessibilityHandler())
                    h->notifyAccessibilityEvent (juce::AccessibilityEvent::titleChanged);
        }

        const Control& getControl() const noexcept { return control; }

        void press()
        {
            if (control.press != nullptr)
                control.press();

            notifyValueChanged();
        }

        void setValue (double v)
        {
            if (control.setValue == nullptr)
                return;

            control.setValue (control.range.clipValue (v));
            notifyValueChanged();
        }

        double getValue() const    { return control.getValue != nullptr ? control.getValue() : 0.0; }

        bool keyPressed (const juce::KeyPress& key) override
        {
            if (control.role != Role::slider)
            {
                if (key.isKeyCode (juce::KeyPress::spaceKey) || key.isKeyCode (juce::KeyPress::returnKey))
                {
                    press();
                    return true;
                }

                return false;
            }

            const bool fine = key.getModifiers().isShiftDown();
            const double small = fine ? control.fineStep : control.step;

            if (key.isKeyCode (juce::KeyPress::upKey))        { setValue (getValue() + small); return true; }
            if (key.isKeyCode (juce::KeyPress::downKey))      { setValue (getValue() - small); return true; }
            if (key.isKeyCode (juce::KeyPress::pageUpKey))    { setValue (getValue() + control.bigStep); return true; }
            if (key.isKeyCode (juce::KeyPress::pageDownKey))  { setValue (getValue() - control.bigStep); return true; }
            if (key.isKeyCode (juce::KeyPress::homeKey))      { setValue (control.resetValue); return true; }

            return false;
        }

        void focusGained (FocusChangeType) override { repaintFocusRing(); }
        void focusLost (FocusChangeType) override   { repaintFocusRing(); }

        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
        {
            juce::AccessibilityActions actions;
            juce::AccessibilityHandler::Interfaces interfaces;
            auto role = juce::AccessibilityRole::button;

            if (control.role == Role::slider)
            {
                role = juce::AccessibilityRole::slider;
                interfaces = juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) };
            }
            else
            {
                actions.addAction (juce::AccessibilityActionType::press, [this] { press(); });

                if (control.role == Role::toggle)
                {
                    role = juce::AccessibilityRole::toggleButton;
                    actions.addAction (juce::AccessibilityActionType::toggle, [this] { press(); });
                }
            }

            return std::make_unique<Handler> (*this, role, std::move (actions), std::move (interfaces));
        }

    private:
        struct Handler final : juce::AccessibilityHandler
        {
            Handler (Proxy& p, juce::AccessibilityRole role, juce::AccessibilityActions actions, Interfaces interfaces)
                : juce::AccessibilityHandler (p, role, std::move (actions), std::move (interfaces)), proxy (p) {}

            juce::AccessibleState getCurrentState() const override
            {
                auto state = juce::AccessibilityHandler::getCurrentState();

                if (proxy.control.role == Role::toggle)
                {
                    state = state.withCheckable();

                    if (proxy.control.isOn != nullptr && proxy.control.isOn())
                        state = state.withChecked();
                }

                return state;
            }

            Proxy& proxy;
        };

        struct ValueInterface final : juce::AccessibilityValueInterface
        {
            explicit ValueInterface (Proxy& p) : proxy (p) {}

            bool isReadOnly() const override                        { return proxy.control.setValue == nullptr; }
            double getCurrentValue() const override                 { return proxy.getValue(); }
            void setValue (double v) override                       { proxy.setValue (v); }
            void setValueAsString (const juce::String& s) override  { proxy.setValue (s.getDoubleValue()); }

            juce::String getCurrentValueAsString() const override
            {
                return proxy.control.valueText != nullptr ? proxy.control.valueText (getCurrentValue())
                                                          : juce::String (getCurrentValue(), 2);
            }

            AccessibleValueRange getRange() const override
            {
                return { { proxy.control.range.getStart(), proxy.control.range.getEnd() }, proxy.control.step };
            }

            Proxy& proxy;
        };

        void notifyValueChanged()
        {
            if (auto* h = getAccessibilityHandler())
                h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
        }

        void repaintFocusRing()
        {
            if (auto* parent = getParentComponent())
                parent->repaint (getBounds().expanded (3));
        }

        Control control;
    };

    /** The proxies of one view, kept in step with its painted layout. */
    class ProxyPool
    {
    public:
        explicit ProxyPool (juce::Component& ownerToUse) : owner (ownerToUse) {}

        ~ProxyPool()
        {
            for (auto& [id, proxy] : proxies)
                owner.removeChildComponent (proxy.get());
        }

        /** Makes the proxies match `controls`. Their order is the Tab order. */
        void sync (std::vector<Control> controls)
        {
            std::map<juce::String, std::unique_ptr<Proxy>> next;
            int order = 1;

            for (auto& c : controls)
            {
                auto it = proxies.find (c.id);
                std::unique_ptr<Proxy> proxy;

                if (it != proxies.end())
                {
                    proxy = std::move (it->second);
                    proxies.erase (it);
                }
                else
                {
                    proxy = std::make_unique<Proxy>();
                    owner.addAndMakeVisible (*proxy);
                }

                proxy->setExplicitFocusOrder (order++);
                const auto id = c.id;
                proxy->update (std::move (c));
                next[id] = std::move (proxy);
            }

            for (auto& [id, stale] : proxies)
                owner.removeChildComponent (stale.get());

            proxies = std::move (next);
        }

        Proxy* find (const juce::String& id) const
        {
            auto it = proxies.find (id);
            return it != proxies.end() ? it->second.get() : nullptr;
        }

        Proxy* getFocused() const
        {
            for (auto& [id, proxy] : proxies)
                if (proxy->hasKeyboardFocus (false))
                    return proxy.get();

            return nullptr;
        }

        int size() const noexcept   { return (int) proxies.size(); }

        template <typename Fn>
        void forEach (Fn&& fn) const
        {
            for (auto& [id, proxy] : proxies)
                fn (*proxy);
        }

        /** Call from the owner's paintOverChildren(). */
        void paintFocusRing (juce::Graphics& g) const
        {
            if (auto* p = getFocused())
            {
                g.setColour (Theme::accent);
                g.drawRoundedRectangle (p->getBounds().toFloat().expanded (1.5f), 3.0f, 1.5f);
            }
        }

    private:
        juce::Component& owner;
        std::map<juce::String, std::unique_ptr<Proxy>> proxies;
    };
}
