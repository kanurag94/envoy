Extended the experimental reverse tunnel handshake
:ref:`jwt_validation <envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.ReverseTunnel.jwt_validation>`
with support for fetching the JWKS from a remote server via
:ref:`remote_jwks <envoy_v3_api_field_extensions.filters.network.reverse_tunnel.v3.JwtHandshakeValidation.remote_jwks>`,
reusing jwt_authn's :ref:`RemoteJwks <envoy_v3_api_msg_extensions.filters.http.jwt_authn.v3.RemoteJwks>`
(``http_uri``, ``cache_duration``, ``async_fetch``, ``retry_policy``). The keys are fetched and
refreshed in the background -- at startup and every ``cache_duration`` -- so handshake verification
stays synchronous. While no keys are cached (before the first successful fetch, or during a
persistent fetch outage) tokens cannot be verified and are treated as invalid.
