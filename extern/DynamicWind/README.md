# Dynamic Wind provider API

`DynamicWindAPI.h` is an unchanged copy of the version 3 provider header from
the local `Dynamic-Wind-integration` checkout, branch
`external-wind-provider-api`. It exposes generic provider callbacks; neither
Open Wind nor Open Shaders is part of that host contract.

Physics responses contain raw XYZ wind, a `pushResponse` choice of `Linear` or
`Cubic`, and a separate `pushSensitivity` multiplier. Cubic is the API default;
Open Wind explicitly requests Linear by default. The host applies the curve
before sensitivity. Both curves match at `PushReferenceStrength` (0.5); other
handler types ignore these push options. Native fallback retains the original
Dynamic Wind impulse calculation.

The optional `OpenWindDynamicWind.dll` bridge reuses the companion
`Open-Wind-Field` implementation and
requires a host exporting `RequestDynamicWindAPI` from `DynamicWind.dll`.
The source checkout supplied no separate license file for this API header.
