package com.nexus.mobile;

import android.app.Activity;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.webkit.SslErrorHandler;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import com.google.firebase.FirebaseApp;
import com.google.firebase.auth.FirebaseAuth;
import com.google.firebase.auth.FirebaseUser;

public final class MainActivity extends Activity {
    private static final String SERVER_URL = "server_url";
    private static final int BACKGROUND = Color.rgb(11, 16, 23);
    private static final int PANEL = Color.rgb(18, 26, 36);
    private static final int FOREGROUND = Color.rgb(237, 244, 247);
    private static final int MUTED = Color.rgb(143, 162, 177);
    private static final int GOLD = Color.rgb(233, 185, 73);
    private static final int CYAN = Color.rgb(89, 212, 196);

    private WebView webView;
    private TextView status;
    private Uri serverOrigin;
    private FirebaseAuth firebaseAuth;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window window = getWindow();
        window.setStatusBarColor(BACKGROUND);
        window.setNavigationBarColor(BACKGROUND);

        FirebaseApp firebaseApp = FirebaseApp.initializeApp(this);
        if (firebaseApp == null) {
            showAuth("Firebase is not configured. Add google-services.json and rebuild.");
            return;
        }
        firebaseAuth = FirebaseAuth.getInstance(firebaseApp);
        if (firebaseAuth.getCurrentUser() == null) {
            showAuth("");
            return;
        }
        showSetup("");
    }

    private void showAuth(String error) {
        releaseWebView();
        LinearLayout page = column();
        page.setPadding(dp(24), dp(40), dp(24), dp(24));

        TextView brand = text("NEXUS", 34, GOLD);
        page.addView(brand, matchWrap());
        TextView subtitle = text("Sign in to your account", 18, FOREGROUND);
        LinearLayout.LayoutParams subtitleParams = matchWrap();
        subtitleParams.topMargin = dp(20);
        page.addView(subtitle, subtitleParams);

        EditText emailInput = new EditText(this);
        emailInput.setSingleLine(true);
        emailInput.setTextColor(FOREGROUND);
        emailInput.setHintTextColor(MUTED);
        emailInput.setHint("Email");
        emailInput.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS);
        emailInput.setAutofillHints(View.AUTOFILL_HINT_EMAIL_ADDRESS);
        emailInput.setPadding(dp(14), dp(12), dp(14), dp(12));
        emailInput.setBackgroundColor(PANEL);
        LinearLayout.LayoutParams emailParams = matchWrap();
        emailParams.topMargin = dp(24);
        page.addView(emailInput, emailParams);

        EditText passwordInput = new EditText(this);
        passwordInput.setSingleLine(true);
        passwordInput.setTextColor(FOREGROUND);
        passwordInput.setHintTextColor(MUTED);
        passwordInput.setHint("Password");
        passwordInput.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD);
        passwordInput.setAutofillHints(View.AUTOFILL_HINT_PASSWORD);
        passwordInput.setPadding(dp(14), dp(12), dp(14), dp(12));
        passwordInput.setBackgroundColor(PANEL);
        LinearLayout.LayoutParams passwordParams = matchWrap();
        passwordParams.topMargin = dp(12);
        page.addView(passwordInput, passwordParams);

        TextView feedback = text(error, 14, Color.rgb(255, 140, 140));
        LinearLayout.LayoutParams feedbackParams = matchWrap();
        feedbackParams.topMargin = dp(10);
        page.addView(feedback, feedbackParams);

        LinearLayout actions = new LinearLayout(this);
        actions.setOrientation(LinearLayout.HORIZONTAL);
        LinearLayout.LayoutParams actionsParams = matchWrap();
        actionsParams.topMargin = dp(16);
        page.addView(actions, actionsParams);

        Button signInButton = new Button(this);
        signInButton.setText("SIGN IN");
        signInButton.setTextColor(BACKGROUND);
        signInButton.setBackgroundTintList(
                android.content.res.ColorStateList.valueOf(CYAN));
        actions.addView(signInButton, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        Button createAccountButton = new Button(this);
        createAccountButton.setText("CREATE ACCOUNT");
        createAccountButton.setTextColor(GOLD);
        actions.addView(createAccountButton, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1));

        boolean authConfigured = firebaseAuth != null;
        signInButton.setEnabled(authConfigured);
        createAccountButton.setEnabled(authConfigured);
        if (authConfigured) {
            signInButton.setOnClickListener(view -> authenticate(
                    emailInput, passwordInput, feedback, signInButton,
                    createAccountButton, false));
            createAccountButton.setOnClickListener(view -> authenticate(
                    emailInput, passwordInput, feedback, signInButton,
                    createAccountButton, true));
        } else if (error.isEmpty()) {
            feedback.setText("Firebase is not configured for this app.");
        }

        TextView note = text(
                "Your password is used only to sign in and is not saved by NEXUS. "
                        + "Firebase keeps your session on this device until you sign out.",
                13,
                MUTED);
        LinearLayout.LayoutParams noteParams = matchWrap();
        noteParams.topMargin = dp(18);
        page.addView(note, noteParams);
        setContentView(page);
    }

    private void authenticate(EditText emailInput, EditText passwordInput,
                              TextView feedback, Button signInButton,
                              Button createAccountButton, boolean createAccount) {
        String email = emailInput.getText().toString().trim();
        String password = passwordInput.getText().toString();
        if (email.isEmpty() || password.isEmpty()) {
            feedback.setText("Enter your email and password.");
            return;
        }

        signInButton.setEnabled(false);
        createAccountButton.setEnabled(false);
        feedback.setText(createAccount ? "Creating account…" : "Signing in…");
        com.google.android.gms.tasks.Task<com.google.firebase.auth.AuthResult> task =
                createAccount
                        ? firebaseAuth.createUserWithEmailAndPassword(email, password)
                        : firebaseAuth.signInWithEmailAndPassword(email, password);
        task.addOnCompleteListener(this, result -> {
            passwordInput.setText("");
            signInButton.setEnabled(true);
            createAccountButton.setEnabled(true);
            if (!result.isSuccessful()) {
                Exception failure = result.getException();
                feedback.setText(failure == null
                        ? "Sign-in failed."
                        : failure.getLocalizedMessage());
                return;
            }
            showSetup("");
        });
    }

    private void showSetup(String error) {
        if (firebaseAuth == null || firebaseAuth.getCurrentUser() == null) {
            showAuth("");
            return;
        }
        releaseWebView();
        LinearLayout page = column();
        page.setPadding(dp(24), dp(40), dp(24), dp(24));

        TextView brand = text("NEXUS", 34, GOLD);
        brand.setGravity(Gravity.CENTER_VERTICAL);
        page.addView(brand, matchWrap());

        TextView subtitle = text("Connect your secure NEXUS server", 18, FOREGROUND);
        LinearLayout.LayoutParams subtitleParams = matchWrap();
        subtitleParams.topMargin = dp(20);
        page.addView(subtitle, subtitleParams);

        TextView explanation = text(
                "The app is ready to install. Live MT5 data will appear after a secure NEXUS "
                        + "server is set up. You do not need to enter MT5 passwords here.",
                15,
                MUTED);
        LinearLayout.LayoutParams explanationParams = matchWrap();
        explanationParams.topMargin = dp(10);
        page.addView(explanation, explanationParams);

        FirebaseUser currentUser = firebaseAuth.getCurrentUser();
        TextView signedIn = text("Signed in as " + currentUser.getEmail(), 13, MUTED);
        LinearLayout.LayoutParams signedInParams = matchWrap();
        signedInParams.topMargin = dp(18);
        page.addView(signedIn, signedInParams);

        Button signOutButton = new Button(this);
        signOutButton.setText("SIGN OUT");
        signOutButton.setTextColor(GOLD);
        LinearLayout.LayoutParams signOutParams = matchWrap();
        signOutParams.topMargin = dp(8);
        page.addView(signOutButton, signOutParams);
        signOutButton.setOnClickListener(view -> signOut());

        EditText urlInput = new EditText(this);
        urlInput.setSingleLine(true);
        urlInput.setTextColor(FOREGROUND);
        urlInput.setHintTextColor(MUTED);
        urlInput.setHint("https://your-nexus-server.example");
        urlInput.setText(getPreferences(MODE_PRIVATE).getString(SERVER_URL, ""));
        urlInput.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_VARIATION_URI);
        urlInput.setPadding(dp(14), dp(12), dp(14), dp(12));
        urlInput.setBackgroundColor(PANEL);
        LinearLayout.LayoutParams inputParams = matchWrap();
        inputParams.topMargin = dp(24);
        page.addView(urlInput, inputParams);

        TextView errorText = text(error, 14, Color.rgb(255, 140, 140));
        LinearLayout.LayoutParams errorParams = matchWrap();
        errorParams.topMargin = dp(8);
        page.addView(errorText, errorParams);

        Button connectButton = new Button(this);
        connectButton.setText("CONNECT");
        connectButton.setTextColor(BACKGROUND);
        connectButton.setBackgroundTintList(
                android.content.res.ColorStateList.valueOf(CYAN));
        LinearLayout.LayoutParams buttonParams = matchWrap();
        buttonParams.topMargin = dp(16);
        page.addView(connectButton, buttonParams);
        connectButton.setOnClickListener(view -> {
            String value = urlInput.getText().toString().trim();
            if (!isSecureServerUrl(value)) {
                errorText.setText("Enter a server address starting with https://");
                return;
            }
            getPreferences(MODE_PRIVATE).edit().putString(SERVER_URL, value).apply();
            connect(value);
        });

        setContentView(page);
    }

    private void connect(String serverUrl) {
        serverOrigin = Uri.parse(serverUrl);

        LinearLayout page = column();
        LinearLayout toolbar = new LinearLayout(this);
        toolbar.setGravity(Gravity.CENTER_VERTICAL);
        toolbar.setPadding(dp(16), dp(4), dp(8), dp(4));
        toolbar.setBackgroundColor(PANEL);

        TextView brand = text("NEXUS", 20, GOLD);
        toolbar.addView(brand, new LinearLayout.LayoutParams(0, dp(52), 1));

        Button serverButton = new Button(this);
        serverButton.setText("SERVER");
        serverButton.setTextColor(CYAN);
        serverButton.setOnClickListener(view -> showSetup(""));
        toolbar.addView(serverButton, wrapWrap());
        Button signOutButton = new Button(this);
        signOutButton.setText("SIGN OUT");
        signOutButton.setTextColor(GOLD);
        signOutButton.setOnClickListener(view -> signOut());
        toolbar.addView(signOutButton, wrapWrap());
        page.addView(toolbar, matchWrap());

        status = text("Connecting securely…", 13, MUTED);
        status.setPadding(dp(16), dp(8), dp(16), dp(8));
        page.addView(status, matchWrap());

        webView = new WebView(this);
        webView.setBackgroundColor(BACKGROUND);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        settings.setSafeBrowsingEnabled(true);
        webView.setWebChromeClient(new WebChromeClient());
        webView.setWebViewClient(new WebViewClient() {
            @Override
            public boolean shouldOverrideUrlLoading(WebView view,
                                                    WebResourceRequest request) {
                return !isSameOrigin(request.getUrl());
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                if (status != null) status.setText("Connected to NEXUS");
            }

            @Override
            public void onReceivedError(WebView view, WebResourceRequest request,
                                        WebResourceError error) {
                if (request.isForMainFrame() && status != null) {
                    status.setText("Could not reach the server. Check the HTTPS address and connection.");
                }
            }

            @Override
            public void onReceivedSslError(WebView view, SslErrorHandler handler,
                                           android.net.http.SslError error) {
                handler.cancel();
                if (status != null) status.setText("Secure connection failed. Check the server certificate.");
            }
        });
        page.addView(webView, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        setContentView(page);
        webView.loadUrl(serverUrl);
    }

    private boolean isSecureServerUrl(String value) {
        Uri uri = Uri.parse(value);
        return "https".equalsIgnoreCase(uri.getScheme())
                && uri.getHost() != null
                && uri.getUserInfo() == null
                && uri.getQuery() == null
                && uri.getFragment() == null;
    }

    private boolean isSameOrigin(Uri target) {
        return "https".equalsIgnoreCase(target.getScheme())
                && target.getHost() != null
                && serverOrigin.getHost().equalsIgnoreCase(target.getHost())
                && effectivePort(serverOrigin) == effectivePort(target);
    }

    private int effectivePort(Uri uri) {
        return uri.getPort() == -1 ? 443 : uri.getPort();
    }

    private void signOut() {
        if (firebaseAuth != null) firebaseAuth.signOut();
        getPreferences(MODE_PRIVATE).edit().remove(SERVER_URL).apply();
        showAuth("");
    }

    private void releaseWebView() {
        status = null;
        if (webView != null) {
            webView.stopLoading();
            webView.destroy();
            webView = null;
        }
    }

    @Override
    public void onBackPressed() {
        if (webView != null && webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        releaseWebView();
        super.onDestroy();
    }

    private LinearLayout column() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setBackgroundColor(BACKGROUND);
        return layout;
    }

    private TextView text(String value, int sizeSp, int color) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(sizeSp);
        view.setTextColor(color);
        return view;
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams wrapWrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
