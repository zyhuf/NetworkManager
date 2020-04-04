// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2014 Red Hat, Inc.
 */

#include "nm-default.h"

#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "common.h"
#include "utils.h"
#include "nm-secret-agent-simple.h"
#include "polkit-agent.h"
#include "nm-polkit-listener.h"

static void
usage (void)
{
	g_printerr (_("Usage: nmcli agent { COMMAND | help }\n\n"
	              "COMMAND := { secret | polkit | all }\n\n"
	           ));
}

static void
usage_agent_secret (void)
{
	g_printerr (_("Usage: nmcli agent secret { help }\n"
	              "\n"
	              "Runs nmcli as NetworkManager secret agent. When NetworkManager requires\n"
	              "a password it asks registered agents for it. This command keeps nmcli running\n"
	              "and if a password is required asks the user for it.\n\n"));
}

static void
usage_agent_polkit (void)
{
	g_printerr (_("Usage: nmcli agent polkit { help }\n"
	              "\n"
	              "Registers nmcli as a polkit action for the user session.\n"
	              "When a polkit daemon requires an authorization, nmcli asks the user and gives\n"
	              "the response back to polkit.\n\n"));
}

static void
usage_agent_all (void)
{
	g_printerr (_("Usage: nmcli agent all { help }\n"
	              "\n"
	              "Runs nmcli as both NetworkManager secret and a polkit agent.\n\n"));
}

/* for pre-filling a string to readline prompt */
static char *pre_input_deftext;
static int
set_deftext (void)
{
	if (pre_input_deftext && rl_startup_hook) {
		rl_insert_text (pre_input_deftext);
		g_free (pre_input_deftext);
		pre_input_deftext = NULL;
		rl_startup_hook = NULL;
	}
	return 0;
}

static gboolean
get_secrets_from_user (const NmcConfig *nmc_config,
                       NmcReadlineStatus *readline_status,
                       const char *request_id,
                       const char *title,
                       const char *msg,
                       GPtrArray *secrets)
{
	int i;

	for (i = 0; i < secrets->len; i++) {
		NMSecretAgentSimpleSecret *secret = secrets->pdata[i];
		char *pwd = NULL;

		/* Ask user for the password */
		if (msg)
			g_print ("%s\n", msg);
		if (secret->value) {
			/* Prefill the password if we have it. */
			rl_startup_hook = set_deftext;
			pre_input_deftext = g_strdup (secret->value);
		}
		if (secret->no_prompt_entry_id)
			pwd = nmc_readline (nmc_config, readline_status, "%s: ", secret->pretty_name);
		else
			pwd = nmc_readline (nmc_config, readline_status, "%s (%s): ", secret->pretty_name, secret->entry_id);

		/* No password provided, cancel the secrets. */
		if (!pwd)
			return FALSE;
		g_free (secret->value);
		secret->value = pwd;
	}
	return TRUE;
}

static void
secrets_requested (NMSecretAgentSimple *agent,
                   const char          *request_id,
                   const char          *title,
                   const char          *msg,
                   GPtrArray           *secrets,
                   gpointer             user_data)
{
	NmCli *nmc = user_data;
	gboolean success;

	if (nmc->nmc_config.print_output == NMC_PRINT_PRETTY)
		nmc_terminal_erase_line ();

	success = get_secrets_from_user (&nmc->nmc_config, &nmc->readline_status, request_id, title, msg, secrets);
	nm_secret_agent_simple_response (agent,
	                                 request_id,
	                                 success ? secrets : NULL);
}

static void
polkit_registered (gpointer instance,
                   gpointer user_data)
{
	g_print (_("nmcli successfully registered as a polkit agent.\n"));
}

static void
polkit_error (gpointer instance,
              const char *error,
              gpointer user_data)
{
	NmCli *nmc = user_data;

	nmc_run_status_return (&nmc->run_status,
	                       NMC_RESULT_ERROR_UNKNOWN,
	                       _("Error: polkit agent failed: %s"),
	                       error);
}

static void
do_agent (const NMCCommand *cmd,
          NmCli *nmc,
          int argc,
          const char *const*argv)
{
	gs_free_error GError *error = NULL;
	gboolean with_polkit_agent = NM_IN_STRSET (cmd->cmd, NULL, "all", "polkit");
	gboolean with_secret_agent = NM_IN_STRSET (cmd->cmd, NULL, "all", "secret");

	nm_assert (with_polkit_agent || with_secret_agent);

	if (with_polkit_agent) {
		if (!nmc_polkit_agent_init (nmc, TRUE, &error)) {
			g_dbus_error_strip_remote_error (error);
			nmc_run_status_return (&nmc->run_status,
			                       NMC_RESULT_ERROR_UNKNOWN,
			                       _("Error: polkit agent initialization failed: %s"),
			                       error->message);
			return;
		}
	}

	if (with_secret_agent) {
		nmc->secret_agent = nm_secret_agent_simple_new ("nmcli-agent");
		if (!nmc->secret_agent) {
			nmc_run_status_return (&nmc->run_status,
			                       NMC_RESULT_ERROR_UNKNOWN,
			                       _("Error: secret agent initialization failed"));
			return;
		}
	}

	nmc_run_status_wait_push (&nmc->run_status);

	if (with_polkit_agent) {
		g_signal_connect (nmc->pk_listener,
		                  NM_POLKIT_LISTENER_SIGNAL_ERROR,
		                  G_CALLBACK (polkit_error),
		                  nmc);
		g_signal_connect (nmc->pk_listener,
		                  NM_POLKIT_LISTENER_SIGNAL_REGISTERED,
		                  G_CALLBACK (polkit_registered),
		                  nmc);
	}

	if (with_secret_agent) {
		nm_secret_agent_simple_enable (nmc->secret_agent, NULL);
		g_signal_connect (nmc->secret_agent,
		                  NM_SECRET_AGENT_SIMPLE_REQUEST_SECRETS,
		                  G_CALLBACK (secrets_requested),
		                  nmc);
		g_print (_("nmcli successfully registered as a NetworkManager's secret agent.\n"));
	}
}

void
nmc_command_do_agent (const NMCCommand *cmd, NmCli *nmc, int argc, const char *const*argv)
{
	static const NMCCommand agent_cmds[] = {
	    { "secret",  do_agent, usage_agent_secret, TRUE, TRUE },
	    { "polkit",  do_agent, usage_agent_polkit, TRUE, TRUE },
	    { "all",     do_agent, usage_agent_all,    TRUE, TRUE },
	    { NULL,      do_agent, usage,              TRUE, TRUE },
	};

	next_arg (nmc, &argc, &argv, NULL);
	nmc_do_cmd (nmc, agent_cmds, *argv, argc, argv);
}
