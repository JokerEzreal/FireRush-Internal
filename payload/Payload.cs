using UnityEngine;
using System.Runtime.CompilerServices;

namespace Payload
{
    /// <summary>
    /// Bridge between C# and C++.
    /// Each extern method maps to a C++ function registered via mono_add_internal_call.
    /// </summary>
    public static class Bridge
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void OnDrawMenu();

        [MethodImpl(MethodImplOptions.InternalCall)]
        public static extern void OnUpdateCallback();
    }

    /// <summary>
    /// Entry point called from C++ after the assembly is loaded.
    /// Creates a persistent GameObject with the overlay MonoBehaviour.
    /// </summary>
    public static class Loader
    {
        public static void Init()
        {
            GameObject go = new GameObject("__FireRush_Overlay__");
            go.hideFlags = HideFlags.HideAndDontSave;
            Object.DontDestroyOnLoad(go);
            var comp = go.AddComponent<OverlayBehaviour>();
            comp.hideFlags = HideFlags.HideAndDontSave;
        }
    }

    /// <summary>
    /// Persistent MonoBehaviour that relays Unity callbacks to C++.
    /// Each subsystem gets its own try-catch so that a failure in one
    /// does not prevent other subsystems from running.
    /// </summary>
    public class OverlayBehaviour : MonoBehaviour
    {
        void OnGUI()
        {
            try { Bridge.OnDrawMenu(); }
            catch (System.Exception) { }
        }

        void Update()
        {
            try { Bridge.OnUpdateCallback(); }
            catch (System.Exception) { }
        }
    }
}
