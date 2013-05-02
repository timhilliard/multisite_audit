<?php
/**
 *
 * @file
 *
 */

/**
 * Implements hook_drush_help().
 */
function multisite_audit_drush_help($section) {
  switch ($section) {
  case 'drush:multisite-audit':
    return dt('Audit all sites in a multisite and produce a matrix of the module versions in use, and updates required.');
  }
}

/**
 * Implements hook_drush_command().
 */
function multisite_audit_drush_command() {
  $items = array();

  $items['multisite-audit'] = array(
    'description' => 'Audit all sites in a multisite and produce a matrix of the module versions in use, and updates required.',
    'callback' => 'drush_multisite_audit',
    //'bootstrap' => DRUSH_BOOTSTRAP_DRUSH, // No bootstrap.
    'examples' => array(
      'drush ma' => 'Produce audit.',
      ),
    'aliases' => array('ma'),
    );

  return $items;
}

/**
 * Main routine that calls all functions
 */
function drush_multisite_audit() {
  // We use some of the functions from the update module
  module_load_include('inc', 'update', 'update.fetch');

  $sites = drush_multisite_audit_setup();

  $status = drush_multisite_get_modules($sites);

  drush_multisite_build_report($sites, $status);
}

/**
 * Output the collected data in a matrix
 */
function drush_multisite_build_report($sites, $status) {
  foreach ($status as $site => $modules) {
    foreach ($modules as $module_name => $module) {
      $matrix[$module_name][$site] = $module['version'];
    }
  }

  print "Checking for updates from updates.drupal.org\n";
  foreach ($matrix as $module => $site) {
    $curr_version = drush_multisite_audit_fetch_update_details(array('name' => $module));
    if (!$curr_version) {
      unset($matrix[$module]);
    }
    else {
      $matrix[$module]['@updates'] = $curr_version;
    }
    print ".";
  }
  print "\n\n";

  printf("%-30s", "Modules");
  foreach ($sites as $site) {
    printf("%-25s", $site['directory']);
  }
  printf("%-25s\n", "Available");

  // Finally, output our matrix
  foreach ($matrix as $module => $site_list) {
    printf("%-30s", $module);
    foreach ($site_list as $site => $version) {
      if ($site == '@updates') {
        continue;
      }
      if (isset($matrix[$module]['@updates']) && ($matrix[$module]['@updates'] != $version)) {
        $update = "**";
      }
      else {
        $update = "";
      }
      printf("%-25s", $version . $update);
    }
    printf("%-25s\n", $matrix[$module]['@updates']);
    $update = "";
  }
}

/**
 * Retrieve the entry for the module from updates.drupal.org
 *
 * TODO: Implement some real caching & expiring?
 *
 */
function drush_multisite_audit_fetch_update_details($module) {
  static $fetched = array();

  $url = _update_build_fetch_url($module);
  $fetch_url_base = _update_get_fetch_url_base($module);
  $project_name = $module['name'];

  // Check if its in the static, dont fetch if it is.
  if (!isset($fetched[$module['name']])) {
    $xml = drupal_http_request($url);
    if (!isset($xml->error) && isset($xml->data)) {
      $data = $xml->data;
    }
    
    if (!empty($data)) {
      $available = update_parse_xml($data);
      if (!empty($available)) {
        $latest = array_shift($available['releases']);
        $fetched[$module['name']] = $latest['version'];
        return $latest['version'];
      }
    }
    else {
      return FALSE;
    }
  }
  else {
    return $fetched[$module['name']];
  }
}

/**
 * Get the list of active modules for a site from the database
 */
function drush_multisite_get_modules($sites) {
  $status = array();
  $count = 0;

  print "Extracting module lists\n";

  // Ok, now start the audit.
  foreach ($sites as $details) {
    Database::addConnectionInfo($site_values['database'], 'default', $db_settings);
    //db_set_active($details['connection']['database']);

    // Now get into the real work
    $query = db_select('system', 's');
    $query->fields('s');
    $query->condition('status', '1');
    $query->condition('type', 'module');
    $query->orderby('name', 'asc');

    $results = $query->execute();

    foreach ($results as $record) {
      $info = unserialize($record->info);
      if ($info['package'] != 'Core') {
        $status[$details['directory']][$record->name] = array(
          'name' => $info['name'],
          'version' => $info['version']
        );
      }
      $count++;
    }

    print ".";
  }

  print "\n";

  print "Found ". $count ." modules\n";

  return $status;
}

/**
 * Get the databases for all the multisites
 */
function drush_multisite_audit_setup() {
  $sites = array();

  //$excluded_dirs = array(".", "..", "all", "default");
  $excluded_dirs = array(".", "..", "all");

  if ($handle = @opendir('sites/')) {
    while (false !== ($directory = readdir($handle))) {
      if (is_dir('sites/' . $directory) && !in_array($directory, $excluded_dirs)) {
        $sites[] = array('directory' => $directory);
      }
    }
    closedir($handle);
  }

  print "Found ". count($sites) ." sites\n";

  print "Determining db details\n";
  // Retrieve the connection string for each database
  foreach ($sites as $site => $values) {
    // Use drush to get the login details
    $command = "drush sql-connect -l ". $values['directory'];
    // print $command . "\n";
    $command_output = exec($command);
    $command_output = substr($command_output, 6);
    $command_output = str_replace(array('--'), array(''), $command_output);
    $params = explode(' ', $command_output);
    foreach ($params as $param) {
      list($name, $value) = explode('=', $param);
      $site_values[$name] = $value;
    }

    // Create settings from them
    $db_settings = array(
      'driver' => 'mysql',
      'database' => $site_values['database'],
      'user' => $site_values['user'],
      'password' => $site_values['password'],
      'host' => $site_values['host'],
      'port' => 3306,
      'collation' => 'utf8_general_ci',
    );

    $sites[$site]['connection'] = $db_settings;
    print ".";
  }

  print "\n";

  return $sites;
}

